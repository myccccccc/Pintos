/* First creates an ordinary empty file and then remove it. */

#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void)
{
  CHECK (remove ("sample.txt"), "remove sample.txt");
}
