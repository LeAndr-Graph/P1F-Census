#!/usr/bin/perl
# check.pl -- REP_OWNER must be refused wherever it cannot do its job, and refused PROPERLY:
# non-zero exit, and before RESULT is created. A guard that prints a complaint and then runs
# anyway is worse than none, because the run looks like it was filtered.
use strict;
use warnings;

my $fail = 0;
sub ok  { my ($w) = @_; printf "  PASS  %s\n", $w }
sub bad { my ($w, $g) = @_; $fail++; printf "  FAIL  %s\n        got: %s\n", $w, $g }

my $log = 'owner-guards-1.log';
open my $h, '<', $log or die "cannot open $log: $!\n(the case bat must run before check.pl)\n";
my $all = do { local $/; <$h> };
close $h;

# Each case records: VERDICT <tag> rc=<n> result=<present|absent>
my %v;
$v{$1} = { rc => $2, file => $3 } while $all =~ /^VERDICT (\S+) rc=(\d+) result=(\w+)/mg;

my @cases = (
    ['k14',      'k14 with REP_OWNER'],
    ['k16',      'k16 with REP_OWNER'],
    ['k20',      'k20 with REP_OWNER'],
    ['k18nodrv', 'k18 with REP_OWNER but no REP_F3COMPLETE'],
    ['k14all',   'k14 with REP_OWNERALL'],
);
for my $c (@cases) {
    my ($tag, $what) = @$c;
    my $r = $v{$tag};
    if (!$r)                      { bad "$what is refused", "no VERDICT line for $tag in $log" }
    elsif ($r->{rc} == 0)         { bad "$what exits non-zero", "rc=0 -- it ran" }
    elsif ($r->{file} ne 'absent'){ bad "$what leaves no RESULT file", "RESULT was created ($r->{file})" }
    else                          { ok "$what is refused (rc=$r->{rc}, no RESULT file)" }
}

# The message has to say WHICH constraint was violated, or the guard is a dead end for the reader.
my @msgs = (
    [qr/REP_OWNER[^\n]*\b18\b|blocks?[^\n]*k18|k18[^\n]*only/i, 'a non-k18 refusal explains that blocks are k18-only'],
    [qr/REP_F3COMPLETE/,                                        'the k18 refusal names REP_F3COMPLETE'],
);
for my $m (@msgs) {
    my ($re, $what) = @$m;
    if ($all =~ $re) { ok $what }
    else { bad $what, 'no such message in the log' }
}

print $fail ? "\nowner-guards-1: FAILED ($fail check(s))\n" : "\nowner-guards-1: PASSED\n";
exit($fail ? 1 : 0);
