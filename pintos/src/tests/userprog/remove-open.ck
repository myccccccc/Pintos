# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(remove-open) begin
(remove-open) remove sample.txt
(remove-open) verified contents of "sample.txt"
(remove-open) close "sample.txt"
(remove-open) end
remove-open: exit(0)
EOF
pass;
