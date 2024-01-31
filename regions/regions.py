#!/usr/bin/env python3
# Copyright 2015 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Authoritative source for Chromium OS region/locale configuration.

Run this module to display all known regions (use --help to see options).
"""

import argparse
import collections.abc
import json
import re
import sys

import yaml  # pylint: disable=import-error


# TBR means you have to get reviewed through go/vpdsettings.
TBR = "To be reviewed"

# Standard keyboard layout for LATAM-es-419 countries.
XKB_LATAM = "xkb:latam::spa"

# The regular expression to check values in Region.keyboards and Region.locales.
# Keyboards should come with xkb: protocol, or the input methods (ime:, m17n:).
# Examples: xkb:us:intl:eng, ime:ime:zh-t:cangjie, xkb:us:altgr-intl:eng
KEYBOARD_PATTERN = re.compile(
    r"^xkb:\w+:[\w-]*:\w+$|" r"^(ime|m17n|t13n):[\w:-]+$"
)
# Locale should be a combination of language and location.
# Examples: en-US, ja.
LOCALE_PATTERN = re.compile(r"^(\w+)(-[A-Z0-9]+)?$")


class Enum(frozenset):
    """An enumeration type.

    Examples:

      To create a enum object:
        dummy_enum = Enum(['A', 'B', 'C'])

      To access a enum object, use:
        dummy_enum.A
        dummy_enum.B
    """

    def __getattr__(self, name):
        if name in self:
            return name
        raise AttributeError


class RegionException(Exception):
    """Exception in Region handling."""


def MakeList(value):
    """Converts the given value to a list.

    Returns:
      A list of elements from "value" if it is iterable (except string);
      otherwise, a list contains only one element.
    """
    if isinstance(value, collections.abc.Iterable) and not isinstance(
        value, str
    ):
        return list(value)
    return [value]


class Region:
    """Comprehensive, standard locale configuration per country/region.

    See :ref:`regions-values` for detailed information on how to set
    these values.
    """

    # pylint gets confused by some of the docstrings.

    # ANSI = US-like
    # ISO = UK-like
    # JIS = Japanese
    # KS = Korean (see http://crosbug.com/p/50753 for why this is not used yet)
    # ABNT2 = Brazilian (like ISO but with an extra key to the left of the
    #   right shift key)
    KeyboardMechanicalLayout = Enum(["ANSI", "ISO", "JIS", "KS", "ABNT2"])

    region_code = None
    """A unique identifier for the region.  This may be a lower-case
    `ISO 3166-1 alpha-2 code
    <http://en.wikipedia.org/wiki/ISO_3166-1_alpha-2>`_ (e.g., ``us``),
    a variant within an alpha-2 entity (e.g., ``ca.fr``), or an
    identifier for a collection of countries or entities (e.g.,
    ``latam-es-419`` or ``nordic``).  See :ref:`region-codes`.

    Note that ``uk`` is not a valid identifier; ``gb`` is used as it is
    the real alpha-2 code for the UK."""

    keyboards = None
    """A list of keyboard layout identifiers (e.g., ``xkb:us:intl:eng``
    or ``m17n:ar``). This field was designed to be the physical keyboard layout
    in the beginning, and then becomes a list of OOBE keyboard selection, which
    then includes non-physical layout elements like input methods (``ime:``).
    To avoid confusion, physical layout is now defined by
    :py:attr:`keyboard_mechanical_layout`, and this is reserved for logical
    layouts.

    This is identical to the legacy VPD ``keyboard_layout`` value."""

    time_zones = None
    """A list of default `tz database time zone
    <http://en.wikipedia.org/wiki/List_of_tz_database_time_zones>`_
    identifiers (e.g., ``America/Los_Angeles``). See
    `timezone_settings.cc <http://goo.gl/WSVUeE>`_ for supported time
    zones.

    This is identical to the legacy VPD ``initial_timezone`` value."""

    locales = None
    """A list of default locale codes (e.g., ``en-US``); see
    `l10n_util.cc <http://goo.gl/kVkht>`_ for supported locales.

    This is identital to the legacy VPD ``initial_locale`` field."""

    keyboard_mechanical_layout = None
    """The keyboard's mechanical layout (``ANSI`` [US-like], ``ISO``
    [UK-like], ``JIS`` [Japanese], ``ABNT2`` [Brazilian] or ``KS`` [Korean])."""

    description = None
    """A human-readable description of the region.
    This defaults to :py:attr:`region_code` if not set."""

    notes = None
    """Implementation notes about the region.  This may be None."""

    regulatory_domain = None
    """An ISO 3166-1 alpha 2 upper-cased two-letter region code for setting
    Wireless regulatory. See crosbug.com/p/38745 for more details.

    When omitted, this will derive from region_code."""

    confirmed = None
    """An optional boolean flag to indicate if the region data is confirmed."""

    FIELDS = [
        "region_code",
        "description",
        "keyboards",
        "time_zones",
        "locales",
        "keyboard_mechanical_layout",
        "regulatory_domain",
    ]
    """Names of fields that define the region."""

    def __init__(
        self,
        region_code,
        keyboards,
        time_zones,
        locales,
        keyboard_mechanical_layout,
        description=None,
        notes=None,
        regdomain=None,
    ):
        """Constructor.

        Args:
          region_code: See :py:attr:`region_code`.
          keyboards: See :py:attr:`keyboards`.  A single string is accepted for
            backward compatibility.
          time_zones: See :py:attr:`time_zones`.
          locales: See :py:attr:`locales`.  A single string is accepted
            for backward compatibility.
          keyboard_mechanical_layout: See :py:attr:`keyboard_mechanical_layout`.
          description: See :py:attr:`description`.
          notes: See :py:attr:`notes`.
          regdomain: See :py:attr:`regulatory_domain`.
        """

        def regdomain_from_region(region):
            if region.find(".") >= 0:
                region = region[: region.index(".")]
            if len(region) == 2:
                return region.upper()
            return None

        # Quick check: should be 'gb', not 'uk'
        if region_code == "uk":
            raise RegionException("'uk' is not a valid region code (use 'gb')")

        self.region_code = region_code
        self.keyboards = MakeList(keyboards)
        self.time_zones = MakeList(time_zones)
        self.locales = MakeList(locales)
        self.keyboard_mechanical_layout = keyboard_mechanical_layout
        self.description = description or region_code
        self.notes = notes
        self.regulatory_domain = regdomain or regdomain_from_region(region_code)
        self.confirmed = None

        for f in (self.keyboards, self.locales):
            assert all(isinstance(x, str) for x in f), (
                "Expected a list of strings, not %r" % f
            )
        for f in self.keyboards:
            assert f is TBR or KEYBOARD_PATTERN.match(
                f
            ), "Keyboard pattern %r does not match %r" % (
                f,
                KEYBOARD_PATTERN.pattern,
            )
        for f in self.locales:
            assert f is TBR or LOCALE_PATTERN.match(
                f
            ), "Locale %r does not match %r" % (
                f,
                LOCALE_PATTERN.pattern,
            )
        assert (
            self.regulatory_domain
            and len(self.regulatory_domain) == 2
            and self.regulatory_domain.upper() == self.regulatory_domain
        ), ("Regulatory domain settings error for region %s" % region_code)

    def __repr__(self):
        return "Region(%s)" % (
            ", ".join([getattr(self, x) for x in self.FIELDS])
        )

    def GetFieldsDict(self):
        """Returns a dict of all substantive fields.

        notes and description are excluded.
        """
        return dict((k, getattr(self, k)) for k in self.FIELDS)


KML = Region.KeyboardMechanicalLayout
PSEUDOLOCALE_REGIONS_LIST = [
    Region(
        "ar.xb",
        "xkb:us::eng",
        "America/Los_Angeles",
        "ar-XB",
        KML.ANSI,
        "Pseudolocale (RTL)",
    ),
    Region(
        "en.xa",
        "xkb:us::eng",
        "America/Los_Angeles",
        "en-XA",
        KML.ANSI,
        "Pseudolocale (long strings)",
    ),
]
REGIONS_LIST = [
    Region(
        "au", "xkb:us::eng", "Australia/Sydney", "en-AU", KML.ANSI, "Australia"
    ),
    Region(
        "be",
        "xkb:be::nld",
        "Europe/Brussels",
        "en-GB",
        KML.ISO,
        "Belgium",
        (
            "Flemish (Belgian Dutch) keyboard; British English language for "
            "neutrality"
        ),
    ),
    Region(
        "br",
        "xkb:br::por",
        "America/Sao_Paulo",
        "pt-BR",
        KML.ABNT2,
        "Brazil (ABNT2)",
        (
            "ABNT2 = ABNT NBR 10346 variant 2. This is the preferred layout "
            "for Brazil. ABNT2 is mostly an ISO layout, but it 12 keys between "
            "the shift keys; see http://goo.gl/twA5tq"
        ),
    ),
    Region(
        "br.abnt",
        "xkb:br::por",
        "America/Sao_Paulo",
        "pt-BR",
        KML.ISO,
        "Brazil (ABNT)",
        (
            "Like ABNT2, but lacking the extra key to the left of the right "
            'shift key found in that layout. ABNT2 (the "br" region) is '
            "preferred to this layout"
        ),
    ),
    Region(
        "br.usintl",
        "xkb:us:intl:eng",
        "America/Sao_Paulo",
        "pt-BR",
        KML.ANSI,
        "Brazil (US Intl)",
        (
            'Brazil with US International keyboard layout. ABNT2 ("br") and '
            'ABNT1 ("br.abnt1 ") are both preferred to this.'
        ),
    ),
    Region(
        "ca.ansi",
        "xkb:us::eng",
        "America/Toronto",
        "en-CA",
        KML.ANSI,
        "Canada (US keyboard)",
        (
            "Canada with US (ANSI) keyboard. Only allowed if there are "
            "separate US English, Canadian English, and French SKUs. "
            "Not for en/fr hybrid ANSI keyboards; for that you would want "
            "ca.hybridansi. See http://goto/cros-canada"
        ),
    ),
    Region(
        "ca.fr",
        "xkb:ca::fra",
        "America/Toronto",
        "fr-CA",
        KML.ISO,
        "Canada (French keyboard)",
        (
            "Canadian French (ISO) keyboard. The most common configuration for "
            "Canadian French SKUs.  See http://goto/cros-canada"
        ),
    ),
    Region(
        "ca.hybrid",
        "xkb:ca:eng:eng",
        "America/Toronto",
        "en-CA",
        KML.ISO,
        "Canada (hybrid ISO)",
        (
            "Canada with hybrid (ISO) xkb:ca:eng:eng + xkb:ca::fra keyboard, "
            "defaulting to English language and keyboard.  Used only if there "
            "needs to be a single SKU for all of Canada.  See "
            "http://goto/cros-canada"
        ),
    ),
    Region(
        "ca.hybridansi",
        "xkb:ca:eng:eng",
        "America/Toronto",
        "en-CA",
        KML.ANSI,
        "Canada (hybrid ANSI)",
        (
            "Canada with hybrid (ANSI) xkb:ca:eng:eng + xkb:ca::fra keyboard, "
            "defaulting to English language and keyboard.  Used only if there "
            "needs to be a single SKU for all of Canada.  See "
            "http://goto/cros-canada"
        ),
    ),
    Region(
        "ca.multix",
        "xkb:ca:multix:fra",
        "America/Toronto",
        "fr-CA",
        KML.ISO,
        "Canada (multilingual)",
        (
            "Canadian Multilingual keyboard; you probably don't want this. See "
            "http://goto/cros-canada"
        ),
    ),
    Region(
        "ch",
        "xkb:ch::ger",
        "Europe/Zurich",
        "de-CH",
        KML.ISO,
        "Switzerland",
        ("German keyboard"),
    ),
    Region("de", "xkb:de::ger", "Europe/Berlin", "de", KML.ISO, "Germany"),
    Region("es", "xkb:es::spa", "Europe/Madrid", "es", KML.ISO, "Spain"),
    Region("fi", "xkb:fi::fin", "Europe/Helsinki", "fi", KML.ISO, "Finland"),
    Region("fr", "xkb:fr::fra", "Europe/Paris", "fr", KML.ISO, "France"),
    Region("gb", "xkb:gb:extd:eng", "Europe/London", "en-GB", KML.ISO, "UK"),
    Region(
        "ie", "xkb:gb:extd:eng", "Europe/Dublin", "en-GB", KML.ISO, "Ireland"
    ),
    Region("in", "xkb:us::eng", "Asia/Calcutta", "en-US", KML.ANSI, "India"),
    Region("it", "xkb:it::ita", "Europe/Rome", "it", KML.ISO, "Italy"),
    Region(
        "latam-es-419",
        "xkb:latam::spa",
        "America/Mexico_City",
        "es-419",
        KML.ISO,
        "Hispanophone Latin America",
        (
            "Spanish-speaking countries in Latin America, using the Iberian "
            "(Spain) Spanish keyboard, which is increasingly dominant in "
            "Latin America. Known to be correct for at least Chile, Colombia, "
            "Mexico, Peru; other es-419 countries may need to be reviewed "
            "through http://goto/vpdsettings. See also http://goo.gl/Iffuqh . "
            "Note that 419 is the UN M.49 region code for Latin America."
        ),
        "MX",
    ),
    Region(
        "my", "xkb:us::eng", "Asia/Kuala_Lumpur", "ms", KML.ANSI, "Malaysia"
    ),
    Region(
        "nl",
        "xkb:us:intl:eng",
        "Europe/Amsterdam",
        "nl",
        KML.ANSI,
        "Netherlands",
    ),
    Region(
        "nordic",
        "xkb:se::swe",
        "Europe/Stockholm",
        "en-US",
        KML.ISO,
        "Nordics",
        (
            "Unified SKU for Sweden, Norway, and Denmark.  This defaults "
            "to Swedish keyboard layout, but starts with US English language "
            "for neutrality.  Use if there is a single combined SKU for Nordic "
            "countries."
        ),
        "SE",
    ),
    Region(
        "nz",
        "xkb:us::eng",
        "Pacific/Auckland",
        "en-NZ",
        KML.ANSI,
        "New Zealand",
    ),
    Region(
        "ph", "xkb:us::eng", "Asia/Manila", "en-US", KML.ANSI, "Philippines"
    ),
    Region(
        "ru",
        ["xkb:us::eng", "xkb:ru::rus"],
        "Europe/Moscow",
        "ru",
        KML.ANSI,
        "Russia",
        ("For R31+ only; R30 and earlier must use US keyboard for login"),
    ),
    Region(
        "se",
        "xkb:se::swe",
        "Europe/Stockholm",
        "sv",
        KML.ISO,
        "Sweden",
        (
            "Use this if there separate SKUs for Nordic countries (Sweden, "
            "Norway, and Denmark), or the device is only shipping to Sweden. "
            "If there is a single unified SKU, use 'nordic' instead."
        ),
    ),
    Region(
        "sg", "xkb:us::eng", "Asia/Singapore", "en-GB", KML.ANSI, "Singapore"
    ),
    Region(
        "us",
        "xkb:us::eng",
        "America/Los_Angeles",
        "en-US",
        KML.ANSI,
        "United States",
    ),
    Region(
        "jp",
        ["xkb:jp::jpn", "ime:jp:mozc_jp"],
        "Asia/Tokyo",
        "ja",
        KML.JIS,
        "Japan",
    ),
    Region(
        "za",
        "xkb:za:gb:eng",
        "Africa/Johannesburg",
        "en-ZA",
        KML.ISO,
        "South Africa",
    ),
    Region(
        "ng", "xkb:us:intl:eng", "Africa/Lagos", "en-GB", KML.ANSI, "Nigeria"
    ),
    Region(
        "hk",
        [
            "xkb:us::eng",
            "ime:zh-t:cangjie",
            "ime:zh-t:quick",
            "ime:zh-t:array",
            "ime:zh-t:dayi",
            "ime:zh-t:zhuyin",
            "ime:zh-t:pinyin",
        ],
        "Asia/Hong_Kong",
        ["zh-TW", "en-GB", "zh-CN"],
        KML.ANSI,
        "Hong Kong",
    ),
    Region(
        "gcc",
        ["xkb:us::eng", "m17n:ar", "t13n:ar"],
        "Asia/Riyadh",
        ["ar", "en-GB"],
        KML.ANSI,
        "Gulf Cooperation Council (GCC)",
        (
            "GCC is a regional intergovernmental political and economic "
            "union consisting of all Arab states of the Persian Gulf except "
            "for Iraq. Its member states are the Islamic monarchies of "
            "Bahrain, Kuwait, Oman, Qatar, Saudi Arabia, and the United Arab "
            "Emirates."
        ),
        "SA",
    ),
    Region(
        "cz",
        ["xkb:cz::cze", "xkb:cz:qwerty:cze"],
        "Europe/Prague",
        ["cs", "en-GB"],
        KML.ISO,
        "Czech Republic",
    ),
    Region(
        "th",
        ["xkb:us::eng", "m17n:th", "m17n:th_pattajoti", "m17n:th_tis"],
        "Asia/Bangkok",
        ["th", "en-GB"],
        KML.ANSI,
        "Thailand",
    ),
    Region(
        "id",
        "xkb:us::ind",
        "Asia/Jakarta",
        ["id", "en-GB"],
        KML.ANSI,
        "Indonesia",
    ),
    Region(
        "tw",
        [
            "xkb:us::eng",
            "ime:zh-t:zhuyin",
            "ime:zh-t:array",
            "ime:zh-t:dayi",
            "ime:zh-t:cangjie",
            "ime:zh-t:quick",
            "ime:zh-t:pinyin",
        ],
        "Asia/Taipei",
        ["zh-TW", "en-US"],
        KML.ANSI,
        "Taiwan",
    ),
    Region(
        "pl",
        "xkb:pl::pol",
        "Europe/Warsaw",
        ["pl", "en-GB"],
        KML.ANSI,
        "Poland",
    ),
    Region(
        "gr",
        ["xkb:us::eng", "xkb:gr::gre", "t13n:el"],
        "Europe/Athens",
        ["el", "en-GB"],
        KML.ANSI,
        "Greece",
    ),
    Region(
        "il",
        ["xkb:us::eng", "xkb:il::heb", "t13n:he"],
        "Asia/Jerusalem",
        ["he", "en-US", "ar"],
        KML.ANSI,
        "Israel",
    ),
    Region(
        "pt",
        "xkb:pt::por",
        "Europe/Lisbon",
        ["pt-PT", "en-GB"],
        KML.ISO,
        "Portugal",
    ),
    Region(
        "ro",
        ["xkb:us::eng", "xkb:ro::rum"],
        "Europe/Bucharest",
        ["ro", "hu", "de", "en-GB"],
        KML.ISO,
        "Romania",
    ),
    Region(
        "kr",
        ["xkb:us::eng", "ime:ko:hangul"],
        "Asia/Seoul",
        ["ko", "en-US"],
        KML.ANSI,
        "South Korea",
    ),
    Region("ae", "xkb:us::eng", "Asia/Dubai", "ar", KML.ANSI, "UAE"),
    Region(
        "za.us",
        "xkb:us::eng",
        "Africa/Johannesburg",
        "en-ZA",
        KML.ANSI,
        "South Africa",
    ),
    Region(
        "vn",
        [
            "xkb:us::eng",
            "m17n:vi_telex",
            "m17n:vi_vni",
            "m17n:vi_viqr",
            "m17n:vi_tcvn",
        ],
        "Asia/Ho_Chi_Minh",
        ["vi", "en-GB", "en-US", "fr", "zh-TW"],
        KML.ANSI,
        "Vietnam",
    ),
    Region(
        "at",
        ["xkb:de::ger", "xkb:de:neo:ger"],
        "Europe/Vienna",
        ["de", "en-GB"],
        KML.ISO,
        "Austria",
    ),
    Region(
        "sk",
        ["xkb:us::eng", "xkb:sk::slo"],
        "Europe/Bratislava",
        ["sk", "hu", "cs", "en-GB"],
        KML.ISO,
        "Slovakia",
    ),
    Region(
        "ch.usintl",
        "xkb:us:intl:eng",
        "Europe/Zurich",
        "en-US",
        KML.ANSI,
        "Switzerland (US Intl)",
        ("Switzerland with US International keyboard layout."),
    ),
    Region("pe", "xkb:latam::spa", "America/Lima", "es-419", KML.ANSI, "Peru"),
    Region(
        "sa",
        "xkb:us::eng",
        "Asia/Riyadh",
        ["ar", "en"],
        KML.ANSI,
        "Saudi Arabia",
    ),
    Region(
        "mx",
        "xkb:latam::spa",
        "America/Mexico_City",
        "es-MX",
        KML.ANSI,
        "Mexico",
    ),
    Region(
        "cl", "xkb:latam::spa", "America/Santiago", "es-419", KML.ANSI, "Chile"
    ),
    Region(
        "kw",
        ["xkb:us::eng", "m17n:ar", "t13n:ar"],
        "Asia/Kuwait",
        ["ar", "en"],
        KML.ANSI,
        "Kuwait",
    ),
    Region(
        "uy",
        "xkb:latam::spa",
        "America/Montevideo",
        "es-419",
        KML.ANSI,
        "Uruguay",
    ),
    Region(
        "tr",
        ["xkb:tr::tur", "xkb:tr:f:tur"],
        "Europe/Istanbul",
        ["tr", "en-GB"],
        KML.ISO,
        "Turkey",
    ),
    Region(
        "ar",
        "xkb:latam::spa",
        "America/Argentina/Buenos_Aires",
        [
            "es-AR",
        ],
        KML.ISO,
        "Argentina",
    ),
    Region(
        "gb.usext",
        "xkb:us:altgr-intl:eng",
        "Europe/London",
        "en-GB",
        KML.ISO,
        "UK (US extended keyboard)",
        ("GB with US extended keyboard"),
    ),
    Region(
        "bg",
        ["xkb:bg::bul", "xkb:bg:phonetic:bul"],
        "Europe/Sofia",
        ["bg", "tr", "en-US"],
        KML.ANSI,
        "Bulgaria",
    ),
    Region(
        "jp.us",
        ["xkb:us::eng", "ime:jp:mozc_us"],
        "Asia/Tokyo",
        "ja",
        KML.ANSI,
        "Japan with US keyboard",
    ),
    Region(
        "is",
        "xkb:is::ice",
        "Atlantic/Reykjavik",
        ["is", "en-GB"],
        KML.ISO,
        "Iceland",
    ),
    Region(
        "us.intl",
        "xkb:us:intl:eng",
        "America/Los_Angeles",
        "en-US",
        KML.ANSI,
        "US (English Intl)",
    ),
    Region(
        "co", "xkb:latam::spa", "America/Bogota", "es-CO", KML.ANSI, "Colombia"
    ),
    Region(
        "hr",
        "xkb:hr::scr",
        "Europe/Zagreb",
        ["hr", "en-GB"],
        KML.ISO,
        "Croatia",
    ),
    Region(
        "kz",
        ["xkb:us::eng", "xkb:kz::kaz", "xkb:ru::rus"],
        ["Asia/Almaty", "Asia/Aqtobe"],
        ["kk", "ru"],
        KML.ANSI,
        "Kazakhstan",
    ),
    Region(
        "ee",
        "xkb:ee::est",
        "Europe/Tallinn",
        ["et", "ru", "en-GB"],
        KML.ISO,
        "Estonia",
    ),
    Region(
        "ro.us",
        ["xkb:us::eng", "xkb:ro::rum"],
        "Europe/Bucharest",
        ["ro", "hu", "de", "en-GB"],
        KML.ANSI,
        "Romania with US keyboard",
    ),
    Region(
        "ua",
        ["xkb:us::eng", "xkb:ua::ukr"],
        ["Europe/Kiev"],
        # "uk" is Ukraine, not United Kingdom.
        ["uk", "en-US"],
        KML.ANSI,
        "Ukraine",
    ),
    Region(
        "ro.usintl",
        ["xkb:us:intl:eng"],
        ["Europe/Bucharest"],
        ["ro", "hu", "de", "en-GB"],
        KML.ANSI,
        "Romania with US International keyboard layout",
    ),
    Region(
        "in.hybrid",
        ["xkb:in::eng", "xkb:us::eng"],
        "Asia/Calcutta",
        ["en-IN", "en-US"],
        KML.ANSI,
        "India with Indian keyboard",
    ),
]

"""A list of :py:class:`regions.Region` objects for
all **confirmed** regions.  A confirmed region is a region whose
properties are known to be correct and valid: all contents (locale / timezone /
keyboards) are supported by Chrome.

NOTE: This list is NOT alpha-sorted. New entries MUST be appended to the end of
the list to retain relative order of existing entries. For backward
compatibility, legacy entries need to stay in the same order because they used
to have numeric mappings.
"""

NOTES_USE_419 = (
    "Use this if there are separate SKUs for Spanish-speaking countries "
    "in Latin America countries, or the device is only shipping to this "
    "region. Otherwise (or if there is a single unified SKU), use "
    "'latam-es-419' region code instead."
)
NOTES_USE_NORDIC = (
    "Use this if there are separate SKUs for Nordic countries (Sweden, "
    "Norway, and Denmark), or the device is only shipping to this region. "
    "Otherwise (or if there is a single unified SKU), use 'nordic' instead."
)

UNCONFIRMED_REGIONS_LIST = [
    Region("ad", TBR, TBR, TBR, TBR, "Andorra"),
    Region("af", TBR, TBR, TBR, TBR, "Afghanistan"),
    Region("ag", TBR, TBR, TBR, TBR, "Antigua and Barbuda"),
    Region("ai", TBR, TBR, TBR, TBR, "Anguilla"),
    Region("al", TBR, TBR, TBR, TBR, "Albania"),
    Region("am", TBR, TBR, TBR, TBR, "Armenia"),
    Region("ao", TBR, TBR, TBR, TBR, "Angola"),
    Region("as", TBR, TBR, TBR, TBR, "American Samoa"),
    Region("aw", TBR, TBR, TBR, TBR, "Aruba"),
    Region("ax", TBR, TBR, TBR, TBR, "Aland Islands"),
    Region("az", TBR, TBR, TBR, TBR, "Azerbaijan"),
    Region("ba", TBR, TBR, TBR, TBR, "Bosnia and Herzegovina"),
    Region("bb", TBR, TBR, TBR, TBR, "Barbados"),
    Region("bd", TBR, TBR, TBR, TBR, "Bangladesh"),
    Region("bf", TBR, TBR, TBR, TBR, "Burkina Faso"),
    Region("bh", TBR, TBR, TBR, TBR, "Bahrain"),
    Region("bi", TBR, TBR, TBR, TBR, "Burundi"),
    Region("bj", TBR, TBR, TBR, TBR, "Benin"),
    Region("bl", TBR, TBR, TBR, TBR, "Saint Barthelemy"),
    Region("bm", TBR, TBR, TBR, TBR, "Bermuda"),
    Region("bn", TBR, TBR, TBR, TBR, "Brunei"),
    Region("bq", TBR, TBR, TBR, TBR, "Bonaire, Saint Eustatius and Saba "),
    Region("bs", TBR, TBR, TBR, TBR, "Bahamas"),
    Region("bt", TBR, TBR, TBR, TBR, "Bhutan"),
    Region("bw", TBR, TBR, TBR, TBR, "Botswana"),
    Region("by", TBR, TBR, TBR, TBR, "Belarus"),
    Region("bz", TBR, TBR, TBR, TBR, "Belize"),
    Region("cc", TBR, TBR, TBR, TBR, "Cocos Islands"),
    Region("cd", TBR, TBR, TBR, TBR, "Democratic Republic of the Congo"),
    Region("cf", TBR, TBR, TBR, TBR, "Central African Republic"),
    Region("cg", TBR, TBR, TBR, TBR, "Republic of the Congo"),
    Region("ci", TBR, TBR, TBR, TBR, "Ivory Coast"),
    Region("ck", TBR, TBR, TBR, TBR, "Cook Islands"),
    Region("cm", TBR, TBR, TBR, TBR, "Cameroon"),
    Region("cn", TBR, TBR, TBR, TBR, "China"),
    Region("cv", TBR, TBR, TBR, TBR, "Cape Verde"),
    Region("cw", TBR, TBR, TBR, TBR, "Curacao"),
    Region("cx", TBR, TBR, TBR, TBR, "Christmas Island"),
    Region("cy", TBR, TBR, TBR, TBR, "Cyprus"),
    Region("dj", TBR, TBR, TBR, TBR, "Djibouti"),
    Region("dm", TBR, TBR, TBR, TBR, "Dominica"),
    Region("dz", TBR, TBR, TBR, TBR, "Algeria"),
    Region("eg", TBR, TBR, TBR, TBR, "Egypt"),
    Region("eh", TBR, TBR, TBR, TBR, "Western Sahara"),
    Region("er", TBR, TBR, TBR, TBR, "Eritrea"),
    Region("et", TBR, TBR, TBR, TBR, "Ethiopia"),
    Region("fj", TBR, TBR, TBR, TBR, "Fiji"),
    Region("fk", TBR, TBR, TBR, TBR, "Falkland Islands"),
    Region("fm", TBR, TBR, TBR, TBR, "Micronesia"),
    Region("fo", TBR, TBR, TBR, TBR, "Faroe Islands"),
    Region("ga", TBR, TBR, TBR, TBR, "Gabon"),
    Region("gd", TBR, TBR, TBR, TBR, "Grenada"),
    Region("ge", TBR, TBR, TBR, TBR, "Georgia"),
    Region("gf", TBR, TBR, TBR, TBR, "French Guiana"),
    Region("gg", TBR, TBR, TBR, TBR, "Guernsey"),
    Region("gh", TBR, TBR, TBR, TBR, "Ghana"),
    Region("gi", TBR, TBR, TBR, TBR, "Gibraltar"),
    Region("gl", TBR, TBR, TBR, TBR, "Greenland"),
    Region("gm", TBR, TBR, TBR, TBR, "Gambia"),
    Region("gn", TBR, TBR, TBR, TBR, "Guinea"),
    Region("gp", TBR, TBR, TBR, TBR, "Guadeloupe"),
    Region("gq", TBR, TBR, TBR, TBR, "Equatorial Guinea"),
    Region(
        "gs", TBR, TBR, TBR, TBR, "South Georgia and the South Sandwich Islands"
    ),
    Region("gu", TBR, TBR, TBR, TBR, "Guam"),
    Region("gw", TBR, TBR, TBR, TBR, "Guinea-Bissau"),
    Region("gy", TBR, TBR, TBR, TBR, "Guyana"),
    Region("ht", TBR, TBR, TBR, TBR, "Haiti"),
    Region("hu", TBR, TBR, TBR, TBR, "Hungary"),
    Region("im", TBR, TBR, TBR, TBR, "Isle of Man"),
    Region("io", TBR, TBR, TBR, TBR, "British Indian Ocean Territory"),
    Region("iq", TBR, TBR, TBR, TBR, "Iraq"),
    Region("ir", TBR, TBR, TBR, TBR, "Iran"),
    Region("je", TBR, TBR, TBR, TBR, "Jersey"),
    Region("jm", TBR, TBR, TBR, TBR, "Jamaica"),
    Region("jo", TBR, TBR, TBR, TBR, "Jordan"),
    Region("ke", TBR, TBR, TBR, TBR, "Kenya"),
    Region("kg", TBR, TBR, TBR, TBR, "Kyrgyzstan"),
    Region("kh", TBR, TBR, TBR, TBR, "Cambodia"),
    Region("ki", TBR, TBR, TBR, TBR, "Kiribati"),
    Region("km", TBR, TBR, TBR, TBR, "Comoros"),
    Region("kn", TBR, TBR, TBR, TBR, "Saint Kitts and Nevis"),
    Region("kp", TBR, TBR, TBR, TBR, "North Korea"),
    Region("ky", TBR, TBR, TBR, TBR, "Cayman Islands"),
    Region("la", TBR, TBR, TBR, TBR, "Laos"),
    Region("lb", TBR, TBR, TBR, TBR, "Lebanon"),
    Region("lc", TBR, TBR, TBR, TBR, "Saint Lucia"),
    Region("li", TBR, TBR, TBR, TBR, "Liechtenstein"),
    Region("lk", TBR, TBR, TBR, TBR, "Sri Lanka"),
    Region("lr", TBR, TBR, TBR, TBR, "Liberia"),
    Region("ls", TBR, TBR, TBR, TBR, "Lesotho"),
    Region("lt", TBR, TBR, TBR, TBR, "Lithuania"),
    Region("lu", TBR, TBR, TBR, TBR, "Luxembourg"),
    Region("lv", TBR, TBR, TBR, TBR, "Latvia"),
    Region("ly", TBR, TBR, TBR, TBR, "Libya"),
    Region("ma", TBR, TBR, TBR, TBR, "Morocco"),
    Region("mc", TBR, TBR, TBR, TBR, "Monaco"),
    Region("md", TBR, TBR, TBR, TBR, "Moldova"),
    Region("me", TBR, TBR, TBR, TBR, "Montenegro"),
    Region("mf", TBR, TBR, TBR, TBR, "Saint Martin"),
    Region("mg", TBR, TBR, TBR, TBR, "Madagascar"),
    Region("mh", TBR, TBR, TBR, TBR, "Marshall Islands"),
    Region("mk", TBR, TBR, TBR, TBR, "Macedonia"),
    Region("ml", TBR, TBR, TBR, TBR, "Mali"),
    Region("mm", TBR, TBR, TBR, TBR, "Myanmar"),
    Region("mn", TBR, TBR, TBR, TBR, "Mongolia"),
    Region("mo", TBR, TBR, TBR, TBR, "Macao"),
    Region("mp", TBR, TBR, TBR, TBR, "Northern Mariana Islands"),
    Region("mq", TBR, TBR, TBR, TBR, "Martinique"),
    Region("mr", TBR, TBR, TBR, TBR, "Mauritania"),
    Region("ms", TBR, TBR, TBR, TBR, "Montserrat"),
    Region("mt", TBR, TBR, TBR, TBR, "Malta"),
    Region("mu", TBR, TBR, TBR, TBR, "Mauritius"),
    Region("mv", TBR, TBR, TBR, TBR, "Maldives"),
    Region("mw", TBR, TBR, TBR, TBR, "Malawi"),
    Region("mz", TBR, TBR, TBR, TBR, "Mozambique"),
    Region("na", TBR, TBR, TBR, TBR, "Namibia"),
    Region("nc", TBR, TBR, TBR, TBR, "New Caledonia"),
    Region("ne", TBR, TBR, TBR, TBR, "Niger"),
    Region("nf", TBR, TBR, TBR, TBR, "Norfolk Island"),
    Region("np", TBR, TBR, TBR, TBR, "Nepal"),
    Region("nr", TBR, TBR, TBR, TBR, "Nauru"),
    Region("nu", TBR, TBR, TBR, TBR, "Niue"),
    Region("om", TBR, TBR, TBR, TBR, "Oman"),
    Region("pf", TBR, TBR, TBR, TBR, "French Polynesia"),
    Region("pg", TBR, TBR, TBR, TBR, "Papua New Guinea"),
    Region("pk", TBR, TBR, TBR, TBR, "Pakistan"),
    Region("pm", TBR, TBR, TBR, TBR, "Saint Pierre and Miquelon"),
    Region("pn", TBR, TBR, TBR, TBR, "Pitcairn"),
    Region("pr", TBR, TBR, TBR, TBR, "Puerto Rico"),
    Region("ps", TBR, TBR, TBR, TBR, "Palestinian Territory"),
    Region("pw", TBR, TBR, TBR, TBR, "Palau"),
    Region("qa", TBR, TBR, TBR, TBR, "Qatar"),
    Region("re", TBR, TBR, TBR, TBR, "Reunion"),
    Region("rs", TBR, TBR, TBR, TBR, "Serbia"),
    Region("rw", TBR, TBR, TBR, TBR, "Rwanda"),
    Region("sb", TBR, TBR, TBR, TBR, "Solomon Islands"),
    Region("sc", TBR, TBR, TBR, TBR, "Seychelles"),
    Region("sd", TBR, TBR, TBR, TBR, "Sudan"),
    Region("sh", TBR, TBR, TBR, TBR, "Saint Helena"),
    Region("si", TBR, TBR, TBR, TBR, "Slovenia"),
    Region("sj", TBR, TBR, TBR, TBR, "Svalbard and Jan Mayen"),
    Region("sl", TBR, TBR, TBR, TBR, "Sierra Leone"),
    Region("sm", TBR, TBR, TBR, TBR, "San Marino"),
    Region("sn", TBR, TBR, TBR, TBR, "Senegal"),
    Region("so", TBR, TBR, TBR, TBR, "Somalia"),
    Region("sr", TBR, TBR, TBR, TBR, "Suriname"),
    Region("ss", TBR, TBR, TBR, TBR, "South Sudan"),
    Region("st", TBR, TBR, TBR, TBR, "Sao Tome and Principe"),
    Region("sx", TBR, TBR, TBR, TBR, "Sint Maarten"),
    Region("sy", TBR, TBR, TBR, TBR, "Syria"),
    Region("sz", TBR, TBR, TBR, TBR, "Swaziland"),
    Region("tc", TBR, TBR, TBR, TBR, "Turks and Caicos Islands"),
    Region("td", TBR, TBR, TBR, TBR, "Chad"),
    Region("tf", TBR, TBR, TBR, TBR, "French Southern Territories"),
    Region("tg", TBR, TBR, TBR, TBR, "Togo"),
    Region("tj", TBR, TBR, TBR, TBR, "Tajikistan"),
    Region("tk", TBR, TBR, TBR, TBR, "Tokelau"),
    Region("tl", TBR, TBR, TBR, TBR, "East Timor"),
    Region("tm", TBR, TBR, TBR, TBR, "Turkmenistan"),
    Region("tn", TBR, TBR, TBR, TBR, "Tunisia"),
    Region("to", TBR, TBR, TBR, TBR, "Tonga"),
    Region("tt", TBR, TBR, TBR, TBR, "Trinidad and Tobago"),
    Region("tv", TBR, TBR, TBR, TBR, "Tuvalu"),
    Region("tz", TBR, TBR, TBR, TBR, "Tanzania"),
    Region("ug", TBR, TBR, TBR, TBR, "Uganda"),
    Region("um", TBR, TBR, TBR, TBR, "United States Minor Outlying Islands"),
    Region("uz", TBR, TBR, TBR, TBR, "Uzbekistan"),
    Region("va", TBR, TBR, TBR, TBR, "Vatican"),
    Region("vc", TBR, TBR, TBR, TBR, "Saint Vincent and the Grenadines"),
    Region("vg", TBR, TBR, TBR, TBR, "British Virgin Islands"),
    Region("vi", TBR, TBR, TBR, TBR, "U.S. Virgin Islands"),
    Region("vu", TBR, TBR, TBR, TBR, "Vanuatu"),
    Region("wf", TBR, TBR, TBR, TBR, "Wallis and Futuna"),
    Region("ws", TBR, TBR, TBR, TBR, "Samoa"),
    Region("ye", TBR, TBR, TBR, TBR, "Yemen"),
    Region("yt", TBR, TBR, TBR, TBR, "Mayotte"),
    Region("zm", TBR, TBR, TBR, TBR, "Zambia"),
    Region("zw", TBR, TBR, TBR, TBR, "Zimbabwe"),
    # Please consider using 'nordic' instead before trying to review 'dk'.
    Region("dk", TBR, TBR, TBR, TBR, "Denmark", NOTES_USE_NORDIC),
    # Please consider using 'nordic' instead before trying to review 'no'.
    Region("no", TBR, TBR, TBR, TBR, "Norway", NOTES_USE_NORDIC),
    # Below are regions already covered by 'latam-es-419' region code.
    # Please consider using 'latam-es-419' before trying to confirm/review them.
    Region("bo", XKB_LATAM, TBR, "es-419", TBR, "Bolivia", NOTES_USE_419),
    Region("cu", XKB_LATAM, TBR, "es-419", TBR, "Cuba", NOTES_USE_419),
    Region("do", XKB_LATAM, TBR, "es-419", TBR, "Dominican Republic",
           NOTES_USE_419),
    Region("ec", XKB_LATAM, TBR, "es-419", TBR, "Ecuador", NOTES_USE_419),
    Region("gt", XKB_LATAM, TBR, "es-419", TBR, "Guatemala", NOTES_USE_419),
    Region("ni", XKB_LATAM, TBR, "es-419", TBR, "Nicaragua", NOTES_USE_419),
    Region("pa", XKB_LATAM, TBR, "es-419", TBR, "Panama", NOTES_USE_419),
    Region("py", XKB_LATAM, TBR, "es-419", TBR, "Paraguay", NOTES_USE_419),
    Region("sv", XKB_LATAM, TBR, "es-419", TBR, "El Salvador", NOTES_USE_419),
    Region("ve", XKB_LATAM, TBR, "es-419", TBR, "Venezuela", NOTES_USE_419),
    Region("hn", XKB_LATAM, TBR, "es-419", TBR, "Honduras", NOTES_USE_419),
    Region("cr", XKB_LATAM, TBR, "es-419", TBR, "Costa Rica", NOTES_USE_419),
]

"""A list of :py:class:`regions.Region` objects for
**unconfirmed** regions. These may contain incorrect information (or not
supported by Chrome browser yet), and all fields must be reviewed before launch.
See http://goto/vpdsettings.

Currently, non-Latin keyboards must use an underlying Latin keyboard
for VPD. (This assumption should be revisited when moving items to
:py:data:`regions.Region.REGIONS_LIST`.)  This is
currently being discussed on <http://crbug.com/325389>.

Some timezones or locales may be missing from ``timezone_settings.cc`` (see
http://crosbug.com/p/23902).  This must be rectified before moving
items to :py:data:`regions.Region.REGIONS_LIST`.
"""


def ConsolidateRegions(regions):
    """Consolidates a list of regions into a dict.

    Args:
      regions: A list of Region objects.  All objects for any given
        region code must be identical or we will throw an exception.
        (We allow duplicates in case identical region objects are
        defined in both regions.py and the overlay, e.g., when moving
        items to the public overlay.)

    Returns:
      A dict from region code to Region.

    Raises:
      RegionException: If there are multiple regions defined for a given
        region, and the values for those regions differ.
    """
    # Build a dict from region_code to the first Region with that code.
    region_dict = {}
    for r in regions:
        existing_region = region_dict.get(r.region_code)
        if existing_region:
            if existing_region.GetFieldsDict() != r.GetFieldsDict():
                raise RegionException(
                    "Conflicting definitions for region %r: %r, %r"
                    % (
                        r.region_code,
                        existing_region.GetFieldsDict(),
                        r.GetFieldsDict(),
                    )
                )
        else:
            region_dict[r.region_code] = r

    return region_dict


def BuildRegionsDict(include_all=False, include_pseudolocales=False):
    """Builds a mapping from code to :py:class:`regions.Region` object.

    ``include_pseudolocales`` should never be true for production builds.

    The regions include:

    * :py:data:`regions.REGIONS_LIST`
    * :py:data:`regions_overlay.REGIONS_LIST`
    * Only if ``include_all`` is true:
      * :py:data:`regions.UNCONFIRMED_REGIONS_LIST`
      * :py:data:`regions.INCOMPLETE_REGIONS_LIST`
    * Only if ``include_pseudolocales`` is true:
      * :py:data:`regions.PSEUDOLOCALE_REGIONS_LIST`

    A region may only appear in one of the above lists, or this function
    will (deliberately) fail.
    """
    regions = list(REGIONS_LIST)
    if include_all:
        known_codes = [r.region_code for r in regions]
        regions += [
            r
            for r in UNCONFIRMED_REGIONS_LIST
            if r.region_code not in known_codes
        ]
    if include_pseudolocales:
        regions += PSEUDOLOCALE_REGIONS_LIST

    # Build dictionary of region code to list of regions with that
    # region code.  Check manually for duplicates, since the region may
    # be present both in the overlay and the public repo.
    return ConsolidateRegions(regions)


REGIONS = BuildRegionsDict()


def main(args=None, out=None):
    if args is None:
        args = sys.argv[1:]

    parser = argparse.ArgumentParser(
        description=("Display all known regions and their parameters. ")
    )
    parser.add_argument(
        "--format",
        choices=("human-readable", "csv", "json", "yaml"),
        default="human-readable",
        help="Output format (default=%(default)s)",
    )
    parser.add_argument(
        "--all",
        action="store_true",
        help="Include unconfirmed and incomplete regions",
    )
    parser.add_argument(
        "--notes", action="store_true", help="Include notes in output"
    )
    parser.add_argument(
        "--include_pseudolocales",
        action="store_true",
        help="Include pseudolocales in output",
    )
    parser.add_argument("--output", default=None, help="Specify output file")
    parser.add_argument(
        "--overlay",
        default=None,
        help="Specify a Python file to overlay extra data",
    )
    args = parser.parse_args(args)

    if args.overlay is not None:
        with open(args.overlay, encoding="utf-8") as f:
            exec(f.read())  # pylint: disable=exec-used

    if args.all:
        # Add an additional 'confirmed' property to help identifying region
        # status for autotests, unit tests and factory module.
        Region.FIELDS.insert(1, "confirmed")
        for r in REGIONS_LIST:
            r.confirmed = True
        for r in UNCONFIRMED_REGIONS_LIST:
            r.confirmed = False

    regions_dict = BuildRegionsDict(args.all, args.include_pseudolocales)

    if out is None:
        if args.output is None:
            out = sys.stdout
        else:
            out = open(
                args.output, "w", encoding="utf-8"
            )  # pylint: disable=consider-using-with

    if args.notes or args.format == "csv":
        Region.FIELDS += ["notes"]

    # Handle YAML and JSON output.
    if args.format in ("yaml", "json"):
        data = {}
        for region in regions_dict.values():
            item = {}
            for field in Region.FIELDS:
                item[field] = getattr(region, field)
            data[region.region_code] = item
        if args.format == "yaml":
            yaml.dump(data, out)
        else:
            json.dump(data, out)
        return

    # Handle CSV or plain-text output: build a list of lines to print.
    lines = [Region.FIELDS]

    def CoerceToString(value):
        """Returns the arguments in simple string type.

        If value is a list, concatenate its values with commas.  Otherwise, just
        return value.
        """
        if isinstance(value, list):
            return ",".join(value)
        else:
            return str(value)

    for region in sorted(regions_dict.values(), key=lambda v: v.region_code):
        lines.append(
            [CoerceToString(getattr(region, field)) for field in Region.FIELDS]
        )

    if args.format == "csv":
        # Just print the lines in CSV format. Note the values may
        # include ',' so the separator must be tab.
        for l in lines:
            print("\t".join(l))
    elif args.format == "human-readable":
        num_columns = len(lines[0])

        # Calculate maximum length of each column.
        max_lengths = []
        for column_no in range(num_columns):
            max_lengths.append(max(len(line[column_no]) for line in lines))

        # Print each line, padding as necessary to the max column length.
        for line in lines:
            for column_no in range(num_columns):
                out.write(line[column_no].ljust(max_lengths[column_no] + 2))
            out.write("\n")
    else:
        sys.exit("Sorry, unknown format specified: %s" % args.format)


if __name__ == "__main__":
    main()
