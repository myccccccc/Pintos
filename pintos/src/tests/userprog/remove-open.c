/* First open a file, then remove it. Then found that we can still readfrom it*/

#include "tests/userprog/sample.inc"
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void)
{
  int fd = open ("sample.txt");
  if (fd == -1)
    fail ("open() returned %d", fd);
  CHECK (remove ("sample.txt"), "remove sample.txt");
  check_file_handle (fd, "sample.txt", sample, sizeof sample - 1);
  msg ("close \"sample.txt\"");
  close(fd);
}
