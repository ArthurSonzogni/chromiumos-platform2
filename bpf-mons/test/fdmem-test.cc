// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * A simple test app which executes some of the functions that
 * fdmon and memmon intercept.
 */

#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>
#include <vector>

#define BUG_ON(cond)                                \
  do {                                              \
    if ((cond)) {                                   \
      printf("BUG at %d %s\n", __LINE__, __func__); \
      exit(1);                                      \
    }                                               \
  } while (0)

static struct option long_options[] = {{"iter", required_argument, 0, 'i'},
                                       {"sleep", required_argument, 0, 's'},
                                       {0, 0, 0, 0}};

class test {
 public:
  virtual ~test() {}
  virtual void execute(void) = 0;
};

/* std::vector tests */
class vector_test : public test {
 private:
  std::vector<std::pair<int, int>> data;

 public:
  vector_test() { printf(":: create vector_test\n"); }
  ~vector_test() {}

  void execute() {
    printf(":: execute vector_test\n");
    for (int i = 0; i < 256; i++) {
      data.push_back({i, i});
    }

    std::vector<std::pair<int, int>> data_copy = data;
    data.clear();
    data_copy.clear();
  }
};

/* std::string tests */
class string_test : public test {
 private:
  std::string data;

 public:
  string_test() { printf(":: create string_test\n"); }
  ~string_test() {}

  void execute() {
    printf(":: execute string_test\n");
    for (int i = 0; i < 256; i++) {
      data += 'G';
    }

    std::string data_copy = data;
    data.clear();
    data_copy.clear();
  }
};

/* char array string tests */
class char_test : public test {
 private:
  char* data;

 public:
  char_test() {
    printf(":: create char_test\n");
    data = new char[256];
    BUG_ON(!data);
    memset(data, 0x00, 256);
  }
  ~char_test() { delete[] data; }

  void execute() {
    printf(":: execute char_test\n");

    memset(data, 'G', 255);

    char* data_copy = strdup(data);
    BUG_ON(!data_copy);
    memset(data_copy, 'g', 255);

    free(data_copy);
  }
};

/* file-table operations tests */
class fd_test : public test {
 public:
  fd_test() { printf(":: create fd_test\n"); }
  ~fd_test() {}

  void execute() {
    int fd0 = open("/dev/G", O_RDONLY);
    BUG_ON(fd0 != -1);

    close(fd0);

    fd0 = open("/dev/zero", O_RDONLY);
    int fd1 = dup(fd0);
    int fd2 = 100;
    int ret = dup2(fd1, fd2);
    BUG_ON(ret == -1);
    close(fd2);
    close(fd1);
    /* Note that we deliberately "leak" fd0 */
  }
};

int main(int argc, char* argv[]) {
  int iter_sleep = 0;
  int num_iter = 1;

  while (1) {
    int option_index = 0, c;

    c = getopt_long(argc, argv, "i:s:", long_options, &option_index);

    /* Detect the end of the options. */
    if (c == -1)
      break;

    switch (c) {
      case 'i':
        num_iter = std::stol(optarg);
        break;
      case 's':
        iter_sleep = std::stol(optarg);
        break;
      default:
        abort();
    }
  }

  for (int iter = 0; iter < num_iter; iter++) {
    class vector_test* v_test = new class vector_test;
    v_test->execute();
    delete v_test;

    class string_test* s_test = new class string_test;
    s_test->execute();
    delete s_test;

    class char_test* c_test = new class char_test;
    c_test->execute();
    delete c_test;

    class fd_test* fd_test = new class fd_test;
    fd_test->execute();
    delete fd_test;

    if (iter < num_iter)
      sleep(iter_sleep);
  }

  // This is sort of important, we need to give the monitor some time to
  // consume and process rb events (which may require /proc/self/maps to
  // still be around, for stack trace decoding)
  sleep(8);

  return 0;
}
