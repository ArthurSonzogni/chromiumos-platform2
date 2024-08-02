// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * A simple test app which executes some of the functions that
 * lockmon intercepts.
 */

#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <mutex>
#include <string>

static struct option long_options[] = {{"test", required_argument, 0, 't'},
                                       {0, 0, 0, 0}};

/*
 * Magic and Safety
 * Make sure you put it at offset(0).
 *
 * See https://github.com/llvm/llvm-project/issues/55431
 */
#define MYSTERIOUS_THING(x) int ____hello_lto[(x)]
#define DO_MYSTERIOUS_THING() ____hello_lto[0] = 0x1337

class test {
 public:
  virtual ~test() {}
  virtual void execute(void) = 0;
};

/* reverse lock dependency chain */
class rdep_test : public test {
 private:
  std::mutex lockA;
  std::mutex lockB;
  std::mutex lockC;

  void acquire_a() { lockA.lock(); }

  void release_a() { lockA.unlock(); }

  void acquire_b() { lockB.lock(); }

  void release_b() { lockB.unlock(); }

  void acquire_c() { lockC.lock(); }

  void release_c() { lockC.unlock(); }

 public:
  rdep_test() { printf(":: create rdep_test\n"); }
  ~rdep_test() {}

  void execute() {
    /* A -> B -> C */
    acquire_a();
    acquire_b();
    acquire_c();
    release_c();
    release_b();
    release_a();

    /* ok: A -> B */
    acquire_a();
    acquire_b();
    release_b();
    release_a();

    /* ok: A -> C */
    acquire_a();
    acquire_c();
    release_c();
    release_a();

    /* not ok: C -> A -> B */
    acquire_c(); /* boom */
    acquire_a();
    acquire_b();
    release_b();
    release_a();
    release_c();
  }
};

/* long reverse lock dependency chain */
class long_rdep_test : public test {
 private:
  MYSTERIOUS_THING(3);
  std::mutex lockP;
  std::mutex lockQ;

  std::mutex lockL;
  std::mutex lockM;

  std::mutex lockX;
  std::mutex lockY;

  void acquire_p() { lockP.lock(); }

  void release_p() { lockP.unlock(); }

  void acquire_q() { lockQ.lock(); }

  void release_q() { lockQ.unlock(); }

  void acquire_l() { lockL.lock(); }

  void release_l() { lockL.unlock(); }

  void acquire_m() { lockM.lock(); }

  void release_m() { lockM.unlock(); }

  void acquire_x() { lockX.lock(); }

  void release_x() { lockX.unlock(); }

  void acquire_y() { lockY.lock(); }

  void release_y() { lockY.unlock(); }

 public:
  long_rdep_test() {
    DO_MYSTERIOUS_THING();
    printf(":: create long_rdep_test\n");
  }
  ~long_rdep_test() {}

  void execute() {
    /* ok: P -> Q */
    acquire_p();
    acquire_q();
    release_q();
    release_p();

    /* ok: X -> Y */
    acquire_x();
    acquire_y();
    release_x();
    release_y();

    /* ok: L -> M */
    acquire_l();
    acquire_m();
    release_m();
    release_l();

    /* not ok: ... M -> ... -> L ... */
    acquire_x();
    release_x();
    acquire_p();
    acquire_x();
    release_x();
    acquire_x();
    acquire_y();
    release_y();
    release_x();
    acquire_m();
    acquire_q();
    acquire_x();
    acquire_y();
    acquire_l(); /* boom */
    release_y();
    release_x();
    release_l();
    release_q();
    release_m();
    release_p();
  }
};

/* recursive locking */
class recursive_test : public test {
 private:
  MYSTERIOUS_THING(5);
  std::mutex lockF;
  std::mutex lockG;

  void acquire_f() { lockF.lock(); }

  void false_acquire_f() {
    /*
     * We don't want to deadlock the test app for real, trylock() is
     * enough to trigger the lockmon.
     */
    lockF.try_lock();
  }

  void release_f() { lockF.unlock(); }

  void acquire_g() { lockG.lock(); }

  void release_g() { lockG.unlock(); }

 public:
  recursive_test() {
    DO_MYSTERIOUS_THING();
    printf(":: create recursive_test\n");
  }
  ~recursive_test() {}

  void execute() {
    acquire_f();
    acquire_g();

    false_acquire_f(); /* boom */

    release_g();
    release_f();
  }
};

/* trylock call/ret handling test */
class trylock_test : public test {
 private:
  MYSTERIOUS_THING(7);
  std::mutex lockI;
  std::mutex lockJ;

  void acquire_i() { lockI.try_lock(); }

  void release_i() { lockI.unlock(); }

  void acquire_j() { lockJ.try_lock(); }

  void release_j() { lockJ.unlock(); }

 public:
  trylock_test() {
    DO_MYSTERIOUS_THING();
    printf(":: create trylock_test\n");
  }
  ~trylock_test() {}

  void execute() {
    acquire_i();
    acquire_j();

    acquire_i(); /* boom */

    release_j();
    release_i();
  }
};

int main(int argc, char* argv[]) {
  int test_nr = 1;

  while (1) {
    int option_index = 0, c;

    c = getopt_long(argc, argv, "t:", long_options, &option_index);

    /* Detect the end of the options. */
    if (c == -1)
      break;

    switch (c) {
      case 't':
        test_nr = std::stol(optarg);
        break;
      default:
        abort();
    }
  }

  class test* t;
  switch (test_nr) {
    case 1:
      t = new class rdep_test;
      break;
    case 2:
      t = new class long_rdep_test;
      break;
    case 3:
      t = new class recursive_test;
      break;
    case 4:
      t = new class trylock_test;
      break;
  }

  t->execute();
  delete t;

  // This is sort of important, we need to give the monitor some time to
  // consume and process rb events (which may require /proc/self/maps to
  // still be around, for stack trace decoding)
  sleep(1);

  return 0;
}
