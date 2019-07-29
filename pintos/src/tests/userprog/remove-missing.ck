# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(remove-missing) begin
(remove-missing) fail to remove no-such-file
(remove-missing) end
remove-missing: exit(0)
EOF
pass;
