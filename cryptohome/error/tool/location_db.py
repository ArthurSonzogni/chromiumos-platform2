#!/usr/bin/env python3

# Copyright 2022 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""This tool maintains locations.h.

This tool generates and maintains the data in error/locations.h and also
verifies that the usage of error location is correct.
"""

import argparse
import bisect
import logging
import operator
import os
import os.path
import re
import subprocess
import sys


class Symbol:
    """Represents a symbol for error location."""

    HEADER_TEMPLATE = ('/* %s */\n'
                       '%s = %d,\n')

    def __init__(self, symbol):
        """Constructor for Symbol.

        Args:
            symbol (str): The representation of the symbol.
        """

        # 'symbol' is the string that represents the symbol.
        # It is the identifier used in the C/C++ source.
        self.symbol = symbol

        # 'allow_dup' is set to true if the configuration file specifically
        # allows this symbol to be used multiple times in the source file.
        self.allow_dup = False

        # 'line_num' is the list of line numbers at which this symbol
        # appeared in the source file. It corresponds 1:1 with
        # self.index_in_file and self.path.
        self.line_num = []

        # 'index_in_file' is the location of the symbol in the source file, in
        # number of characters. It is a list and each element corresponds 1:1
        # with self.line_num and self.path.
        self.index_in_file = []

        # 'path' is the path to the file. It is a list and each element
        # corresponds 1:1 with self.index_in_file and self.line_num.
        self.path = []

        # 'value' is the numeric value of the symbol, if one is assigned.
        # It is the value for the enum in the generated file.
        self.value = None

    def generate_lines(self):
        """Generates the lines for this symbol in locations.h.

        Returns:
            A list of strings that is the lines to be placed in locations.h.
        """

        assert self.value is not None
        return [Symbol.HEADER_TEMPLATE % (self._generate_comments(),
                                          self.symbol, self.value),]

    def _generate_comments(self):
        if self.allow_dup:
            return '=Duplicate Allowed='
        if len(self.line_num) == 0 and len(self.path) == 0:
            return '=Obsolete='
        assert len(self.line_num) == 1 and len(self.path) == 1
        return '%s' % (self.path[0],)

    def merge(self, target):
        """Merges information from another symbol into this object.

        The caller is responsible for destroying target after the call.
        There's no guarantee on the state of target after the call.

        Args:
            target: Another Symbol.
        """

        assert self.symbol == target.symbol
        assert self.allow_dup == target.allow_dup
        assert (len(self.line_num) == len(self.index_in_file) and
                len(self.line_num) == len(self.path))
        assert (len(target.line_num) == len(target.index_in_file) and
                len(target.line_num) == len(target.path))

        self.line_num += target.line_num
        self.path += target.path
        self.index_in_file += target.index_in_file

        if self.value is not None:
            assert target.value is None
        else:
            self.value = target.value

    def __str__(self):
        locs = ','.join(['%s:%d' % x for x in
                         zip(self.path, self.line_num)])
        result = '%s=%s @ %s' % (self.symbol, self.value, locs)
        if self.allow_dup:
            result += ' duplicates allowed'
        return result


class LineNumberFinder:
    """This converts index in file to line number.

    This utility converts position in the file into line number.
    Each instance represents a file.
    """

    def __init__(self, content):
        """Constructor for LineNumberFinder.

        Args:
            content (str): The content of the file.
        """

        # '_content' is the content of the file in string format.
        self._content = content

        # '_line_num_of_index' is the mapping from line number to index.
        # -1 here so that binary search is guaranteed to be bounded and that
        # the line number starts from 1.
        self._line_num_of_index = [-1, 0,]

        self._preprocess()

    def _preprocess(self):
        """Populate self._line_num_of_index."""
        self._line_num_of_index.extend(i for i, c in enumerate(self._content)
                                       if c == '\n')
        self._line_num_of_index.append(len(self._content))

    def find_by_pos(self, idx):
        """Find the line that idx char is on.

        Args:
            idx (int): The location in number of characters.

        Returns:
            An integer that is the line number, it starts from 1.
        """
        return bisect.bisect_right(self._line_num_of_index, idx)-1


class SourceScanner:
    """This scans for error location usage in the source"""

    ERROR_LOC_USAGE_RE = r'CRYPTOHOME_ERR_LOC\(\s*([a-zA-Z][a-zA-Z0-9]*)\s*\)'

    @staticmethod
    def scan_single_file(path):
        """Scan a single file for error location usage.

        Args:
            path (str): The path to the file to scan.

        Returns:
            A list of Symbol, representing the symbols found in the given file.
        """

        logging.debug('Scanning file %s', path)
        with open(path, 'r') as f:
            content = f.read()
        linenum_util = LineNumberFinder(content)
        results = []

        # Search for the target string in the source file.
        pat = re.compile(SourceScanner.ERROR_LOC_USAGE_RE)
        for m in pat.finditer(content):
            loc_name = m.group(1)
            loc_pos = m.start(1)
            symbol = Symbol(loc_name)
            symbol.path.append(path)
            symbol.index_in_file.append(loc_pos)
            symbol.line_num.append(linenum_util.find_by_pos(loc_pos))
            results.append(symbol)

        return results

    @staticmethod
    def scan_directory(path, allowed_ext):
        """Scan a directory recursively for error location usage.

        Args:
            path (str): The path to the directory to scan.
            allowed_ext (Set[str]): Allowed extensions.
                Only scan files with extensions in the Set.

        Returns:
            A list of Symbol, representing the symbols found in the directory.
        """

        logging.debug('Scanning directory %s', path)
        result = []
        for f in os.scandir(path):
            if f.is_dir():
                result += SourceScanner.scan_directory(f.path, allowed_ext)
                continue
            if (f.is_file() and
                    os.path.splitext(f.name.lower())[1] in allowed_ext):
                result += SourceScanner.scan_single_file(f.path)
        return result


class Verifier:
    """Verifies the result from scanning.

    This is used to verify that the result from scanner is the correct usage
    for error location, i.e. there are no duplications outside of the allowed
    ones. It also helps to collate the various symbols together.
    """

    def __init__(self, dup_allowlist):
        """Constructor for Verifier.

        Args:
            dup_allowlist (Set[str]): a set of symbol representation that is in
            the duplication allowlist. If a symbol is in the allowlist, then
            that string can be used multiple times in the source file.
        """

        # '_dup_allowlist' is the duplication allowlist, see comment above.
        self._dup_allowlist = dup_allowlist

    def _update_allow_dup_in_symbols(self, symbols):
        """Update .allow_dup for all symbol in symbols."""

        for sym in symbols:
            if sym.symbol in self._dup_allowlist:
                sym.allow_dup = True

    def collate_and_verify(self, input_symbols):
        """Collate the list of symbols and check for duplications.

        This function collate the symbols by merging the same symbol in the
        `input_symbols` list, and check to see if there's any duplicate for
        symbols not in the `self._dup_allowlist`.

        Args:
            input_symbols (List[Symbol]): A list of symbols from the codebase.

        Returns:
            Tuple[Dict[str, Symbol], Dict[str, Symbol]]: Tuple of
            collated_symbols and violating_dup. `collated_symbols` is the
            collated symbols after removing duplicates. `violating_dup` is the
            set of symbols that are duplicated and not in the allow list.
        """

        self._update_allow_dup_in_symbols(input_symbols)
        collated_symbols = {}
        violating_dup = {}
        for sym in input_symbols:
            dup = collated_symbols.get(sym.symbol)
            if dup:
                if sym.allow_dup:
                    dup.merge(sym)
                else:
                    dup.merge(sym)
                    violating_dup[sym.symbol] = dup
            else:
                # No duplicates.
                collated_symbols[sym.symbol] = sym
        return collated_symbols, violating_dup


class LocationDB:
    """Database in locations.h

    This class manages the mapping between error location symbol and their
    values in locations.h.
    """

    GENERATED_START = ('// Start of generated content. '
                       'Do NOT modify after this line.')
    GENERATED_END = '// End of generated content.'
    EXISTING_RECORDS_RE = (r'\/\*\s*([a-zA-Z0-9:/_.= \n]|\s)*\s*\*\/\s*'
                           r'\s+([a-zA-Z][a-zA-Z0-9]*)\s*\=\s*([0-9]+)\s*,')

    def __init__(self, path, dup_allowlist):
        """Constructor for LocationDB.

        Args:
            path (str): The path to locations.h.
            dup_allowlist (Set[str]): Duplication allowlist, see
            Verifier.__init__'s documentation.
        """

        # 'path' is the path to locations.h.
        self.path = path

        # '_dup_allowlist' is a set that holds the allowlist of symbols that
        # can be used multiple times in the source tree. See Verifier.__init__
        # for more info.
        self._dup_allowlist = dup_allowlist

        # 'symbols' is a dict that maps the symbol's representation (as a str)
        # to the Symbol object. It is None if we are not loaded yet.
        self.symbols = None

        # 'value_to_symbol' is a dict that maps the symbol's value (the integer
        # value of the enum) to the Symbol object. It is None if we are not
        # loaded yet.
        self.value_to_symbol = None

        # '_lines' holds the content of the locations.h file. It is None if we
        # are not loaded yet.
        self._lines = None

        # '_start_line' is the line number in locations.h at which the enum
        # section starts. It is None if we are not loaded yet.
        # Line number starts from 1.
        self._start_line = None

        # '_end_line' is the line number in locations.h at which the enum
        # section ends. It is None if we are not loaded yet.
        # Line number starts from 1.
        self._end_line = None

        # '_next_value' is the next available enum value in locations.h.
        # It is None if we are not loaded yet.
        self._next_value = None

    def _find_generated_marker(self):
        """Finds and sets the start and end of generated marker.

        Returns:
            bool: True iff only one pair of generated marker is found.
        """

        self._start_line = None
        self._end_line = None
        for line_num_index, line in enumerate(self._lines):
            line_num = line_num_index + 1
            if line.strip() == LocationDB.GENERATED_START:
                if self._start_line is not None:
                    logging.error(('Multiple generated starting marker at %d'
                                   ' and %d'), self._start_line, line_num)
                    return False
                self._start_line = line_num
            if line.strip() == LocationDB.GENERATED_END:
                if self._end_line is not None:
                    logging.error(('Multiple generated ending marker at %d '
                                   'and %d'), self._end_line, line_num)
                    return False
                self._end_line = line_num
        if self._start_line is None:
            logging.error('No generated starting marker in locations.h')
            return False
        if self._end_line is None:
            logging.error('No generated ending marker in locations.h')
            return False
        return True

    def _scan_for_existing_records(self, content):
        """Parse all existing records in 'content'."""
        pat = re.compile(LocationDB.EXISTING_RECORDS_RE)
        self.symbols = {}
        for m in pat.finditer(content):
            s = Symbol(m.group(2))
            s.value = int(m.group(3))
            self.symbols[s.symbol] = s
        return len(self.symbols)

    def _build_reverse_map(self):
        """Populate self.value_to_symbol."""
        self.value_to_symbol = {}
        for sym in self.symbols:
            value = self.symbols[sym].value
            if value is not None:
                # symbols are guaranteed to be unique in existing locations.h.
                assert value not in self.value_to_symbol
                self.value_to_symbol[value] = sym

    def _get_generated_lines(self):
        assert self._start_line is not None
        assert self._end_line is not None
        return '\n'.join(self._lines[self._start_line:self._end_line-1])

    def load(self):
        """Load from locations.h.

        This method will load the content of locations.h from `self.path`.

        Returns:
            bool: True if successful.
        """

        with open(self.path, 'r') as f:
            self._lines = f.readlines()
        if not self._find_generated_marker():
            return False
        self._scan_for_existing_records(self._get_generated_lines())
        self._build_reverse_map()
        return True

    def _generate_header_lines(self):
        symbols_list = [s for s in self.symbols.values()]
        symbols_list.sort(key=operator.attrgetter('value'))
        return [sym.generate_lines() for sym in symbols_list]

    def store(self):
        """Save the state in this object back into locations.h.

        This method will convert the state in this object into string content
        to be written back into locations.h, then it'll write the result into
        `self.path`.
        """

        assert self._lines is not None and self.symbols is not None
        result_lines = []
        # Include the portion that is before the generated content.
        result_lines += self._lines[0:self._start_line]
        # Add the generated portion
        result_lines += sum(self._generate_header_lines(), [])
        # Include the portion that is after the generated content.
        result_lines += self._lines[self._end_line-1:]
        with open(self.path, 'w') as f:
            f.write(''.join(result_lines))
        # Invalidate the variables to ensure stale data isn't left behind.
        self.symbols = None
        self._lines = None
        self._start_line = None
        self._end_line = None
        self._next_value = None
        self.value_to_symbol = None
        # Format the result
        subprocess.call(['clang-format', '-i', self.path])

    def update_from_scan_result(self, result):
        """Update the state of this object.

        This method will update the internal state within this object from
        the Symbols found in `result`.

        Args:
            result (Dict[str, Symbol]): A dict of symbols found in source tree.
        """

        # Clear relevant fields in the current symbols.
        # The enums start at 100 because we want to reserve the first 100 enum
        # in case there's any special use case.
        self._next_value = 100
        for sym in self.symbols.values():
            sym.line_num = []
            sym.path = []
            sym.index_in_file = []
            sym.allow_dup = sym.symbol in self._dup_allowlist
            if sym.value:
                self._next_value = max(self._next_value, sym.value+1)

        for sym in result.values():
            if sym.symbol in self.symbols:
                self.symbols[sym.symbol].merge(sym)
            else:
                self.symbols[sym.symbol] = sym
                self.symbols[sym.symbol].value = self._next_value
                self._next_value += 1

        self._build_reverse_map()


class DBTool:
    """Bridge for various classes above.

    This class is in charge of calling the various classes above and bridge
    their input/outputs to each other.
    """

    ALLOWED_SRC_EXT = frozenset({'.cc', '.h'})
    SCAN_DENYLIST = frozenset({'./error/location_utils.h'})
    LOCATIONS_H_PATH = './error/locations.h'

    def __init__(self, allowlist_path):
        """Constructor for DBTool.

        Args:
            allowlist_path (str): The path to the file that stores the content
            of dup_allowlist. Each line is a symbol that is in the allowlist,
            thus each line is a symbol that can appear multiple times in the
            code base.
        """

        # 'db_path' is the path to locations.h.
        self.db_path = DBTool.LOCATIONS_H_PATH

        # 'allowlist_path' is the path to duplication allowlist configuration.
        # See comment in DBTool.__init__() above.
        self.allowlist_path = allowlist_path

        # '_dup_allowlist' is the duplication allowlist, see comment in
        # Verifier.__init__().
        self._dup_allowlist = set({})
        self._load_dup_allowlist()

        # 'verifier' is an instance of Verifier for verifying symbols.
        self.verifier = Verifier(self._dup_allowlist)

        # 'db' is an instance of LocationDB for loading/storing locations.h.
        self.db = LocationDB(self.db_path, self._dup_allowlist)

    def _load_dup_allowlist(self):
        """Load self._dup_allowlist from file."""

        with open(self.allowlist_path, 'r') as f:
            lines = f.readlines()
        lines = [line.strip() for line in lines]
        lines = [line for line in lines if len(line) > 0 and line[0] != '#']
        for line in lines:
            self._dup_allowlist.add(line)

    def check_sources(self):
        """Scan the codebase and check for errors.

        Returns:
            Tuple[bool, Dict[str, Symbol]]: Returns (success, symbols), where
            by success is a True iff the operation is successful and there's no
            error found, and in that case Symbol will be the symbols found in
            the code base.
        """

        # Scan for all symbols.
        all_symbols = SourceScanner.scan_directory('.', DBTool.ALLOWED_SRC_EXT)
        all_symbols = [r for r in all_symbols
                       if r.path[0] not in DBTool.SCAN_DENYLIST]
        collated_symbols, violations = self.verifier.collate_and_verify(
            all_symbols)
        # Notify the user on any violations.
        if len(violations) != 0:
            print('Please remove duplicate usage of error location in code:')
            for s in violations:
                print(violations[s])
            return False, None
        return True, collated_symbols

    def update_location_db(self):
        """Scan the code base and update locations.h

        Scan the code base for all usage of error symbols, then process them
        to see if there's any error. If there's no error, update locations.h.

        Returns:
            bool: True if successful.
        """

        if not self._load_full_db():
            return False
        self.db.store()

        return True

    def _load_full_db(self):
        success, symbols = self.check_sources()
        if not success:
            return False

        # Load the content of the locations.h
        self.db.load()
        self.db.update_from_scan_result(symbols)
        return True

    def lookup_symbol(self, value):
        """Print the usage location for an error ID node.

        Given an error ID node, as in, a symbol, locate where it is used and
        print it out.

        Args:
            value (str): The symbol.
        """

        self._load_full_db()
        if value not in self.db.value_to_symbol:
            print('Value %s not found' % value)
            return False
        symbol = self.db.symbols[self.db.value_to_symbol[value]]
        print('Value %s is %s and can be found at:' % (symbol.value,
                                                       symbol.symbol))
        for path, line in zip(symbol.path, symbol.line_num):
            print('%s:%d'  % (path, line))
        return True

    def decode_stack(self, locs):
        """Print the stack for an error ID.

        Given an error ID (dash-separated symbols), decode the symbols and
        print out their location in the code base.

        Args:
            locs (str): A dash-separated symbols string.
        """

        self._load_full_db()

        stack = [int(x) for x in locs.split('-')]
        for val in stack:
            if val not in self.db.value_to_symbol:
                print('Value %s not found' % val)
            else:
                symbol = self.db.symbols[self.db.value_to_symbol[val]]
                print('%s' % (symbol,))


class DBToolCommandLine:
    """This class handles the command line operations for the tool."""

    def __init__(self):
        """Constructor for DBToolCommandLine."""

        # 'parser' is an ArgumentParser instance for parsing command line
        # arguments.
        self.parser = None

        # 'args' is the arguments parsed by self.parser.
        self.args = None

        # 'db_tool' is an instance of DBTool for carrying out the operations
        # specified in arguments.
        self.db_tool = None

        # 'allowlist_path' is the path to the duplication allowlist
        # configuration.
        self.allowlist_path = None

    def _setup_logging(self):
        logging.basicConfig(level=logging.INFO)

    def _parse_args(self):
        self.parser = argparse.ArgumentParser(description=
                                              'Tool for handling error '
                                              'location in locations.h')
        self.parser.add_argument('--update',
                                 help=('Scan the source directory'
                                       ' and update the locations.h db'),
                                 action='store_true')
        self.parser.add_argument('--check',
                                 help=('Scan the source directory and check '
                                       'that cryptohome error is used '
                                       'correctly.'),
                                 action='store_true')
        self.parser.add_argument('--lookup',
                                 help='Lookup a single error location code',
                                 default=None)
        self.parser.add_argument('--decode',
                                 help=('Decode a stack of error location, ex.'
                                       '42-7-15'),
                                 default=None)
        self.parser.add_argument('--src',
                                 help=('The cryptohome source '
                                       'directory'), default=None)
        self.args = self.parser.parse_args()

    def _goto_srcdir(self):
        assert self.args is not None
        srcdir = self.args.src
        if srcdir is None:
            srcdir = os.path.join(os.path.dirname(__file__), '..', '..')
        srcdir = os.path.abspath(srcdir)
        logging.info('Using cryptohome source at: %s', srcdir)
        os.chdir(srcdir)

    def _get_dup_allowlist_path(self):
        path = os.path.join(os.path.dirname(__file__), 'dup_allowlist.txt')
        path = os.path.abspath(path)
        return path

    def main(self):
        """The main function for this command line tool.

        Returns:
            int: The exit code.
        """
        self._parse_args()
        self._setup_logging()
        self.allowlist_path = self._get_dup_allowlist_path()
        self._goto_srcdir()
        self.db_tool = DBTool(self.allowlist_path)
        if self.args.update:
            self.db_tool.update_location_db()
        elif self.args.check:
            result, _ = self.db_tool.check_sources()
            if not result:
                return 1
        elif self.args.lookup is not None:
            self.db_tool.lookup_symbol(int(self.args.lookup))
        elif self.args.decode is not None:
            self.db_tool.decode_stack(self.args.decode)
        else:
            logging.error('No action specified, please see --help')
            return 1
        return 0


# Invoke the main function for the tool.
if __name__ == '__main__':
    cmdline = DBToolCommandLine()
    return_value = cmdline.main()
    sys.exit(return_value)
