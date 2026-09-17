@echo off
REM ============================================================================
REM  env_reset.bat -- clear EVERY REP_* variable the engines read, plus RESULT.  Called
REM  (CALL, so it lands in the caller's setlocal scope) at the top of every runs/ and
REM  regression/ case bat: a leftover REP_* in the shell would silently change what the
REM  case searches, and the regression compare is byte-exact.
REM
REM  ExtPrint is deliberately NOT cleared.  It sets how much of the run's own bookkeeping
REM  reaches the log and cannot change what a run finds, so leaving it alone lets you trace
REM  any case with  SET ExtPrint=1  in the shell, without editing the case bat.
REM ============================================================================
for %%V in (
  REP_BLOCKDUMP REP_BLOCKSFOR REP_CANONCOUNT REP_CANONDIAG REP_CANONKEY
  REP_CANONOVL REP_CANONTEST REP_CDUMP REP_CLASSBLOCKS REP_CMAP REP_COMPLETEPREFIX
  REP_CPCAP REP_DIAGPROBE REP_DIAGPRUNE REP_DIAGSUB REP_DIVE REP_DUMPFIRST REP_DUMPREPS
  REP_ESTFILE REP_ESTIMATE REP_EVENTFILE REP_F17DUMP REP_F17SUB REP_F2COUNT REP_F2FIXED
  REP_F2MAX REP_F3A REP_F3COMPLETE REP_F3COUNT REP_F3DUMP REP_F3LIST
  REP_F3RAW REP_F3START REP_F3STOP REP_FANOUT REP_FPF REP_FTEST REP_LEVEL REP_LEVELSTATS
  REP_LOCATE REP_NAVCAP REP_NAVDUMP REP_NAVEST REP_NAVIGATE REP_NAVMAXBLK REP_NAVRES
  REP_NAVSTEP REP_NAVSTEPS REP_NOPRUNE REP_NOSKIP REP_ONLYTYPES REP_ORDER REP_ORDERS
  REP_PATAPPLY REP_PATBYCLASS REP_PATHCANON REP_PATHCOORD REP_PATHCOORDCANON
  REP_PATHCOORDDIAG REP_PATLEVEL REP_PATONLY REP_PATPAIR REP_PATSTAT REP_PATSUB
  REP_PATTERN REP_PRINTEACH REP_PRINTRAW REP_PRIORITY REP_PRUNELEVEL REP_RANGE
  REP_RAWSHARD REP_REPSCHECK REP_ROW4 REP_SAMPLE REP_SGEN REP_SWAPCAP REP_SYMCAP REP_TYPEMASK
  REP_VTEST REP_WITNESS REP_OWNER REP_OWNERALL
  RESULT
) do SET "%%V="
exit /b 0
