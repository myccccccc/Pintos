# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(cache-hit-rate) begin
(cache-hit-rate) create "test1"
(cache-hit-rate) open "test1"
(cache-hit-rate) read "test1"
(cache-hit-rate) read "test1"
(cache-hit-rate) read "test1"
(cache-hit-rate) read "test1"
(cache-hit-rate) read "test1"
(cache-hit-rate) read "test1"
(cache-hit-rate) read "test1"
(cache-hit-rate) read "test1"
(cache-hit-rate) open "test1"
(cache-hit-rate) read "test1"
(cache-hit-rate) read "test1"
(cache-hit-rate) read "test1"
(cache-hit-rate) read "test1"
(cache-hit-rate) read "test1"
(cache-hit-rate) read "test1"
(cache-hit-rate) read "test1"
(cache-hit-rate) read "test1"
(cache-hit-rate) Hot hit rate is 8
(cache-hit-rate) Cold hit rate is 7
(cache-hit-rate) Verifying that cache hit rate has improved...
(cache-hit-rate) end
EOF
pass;
