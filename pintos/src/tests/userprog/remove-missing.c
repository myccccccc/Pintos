/* Remove a file that is missing. */

#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void)
{
  CHECK (!remove ("no-such-file"), "fail to remove no-such-file");
}
