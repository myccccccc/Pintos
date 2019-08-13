# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(write-coalesce) begin
(write-coalesce) create "test2"
(write-coalesce) open "test2"
(write-coalesce) writing done
(write-coalesce) resetting file position
(write-coalesce) reading done
(write-coalesce) Verifying order of device writes...
(write-coalesce) end
EOF
pass;
