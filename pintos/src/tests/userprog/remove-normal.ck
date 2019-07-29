# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(remove-normal) begin
(remove-normal) remove sample.txt
(remove-normal) end
remove-normal: exit(0)
EOF
pass;
