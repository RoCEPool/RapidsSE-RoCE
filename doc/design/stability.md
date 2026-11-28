# Stability: low memory, undefined words, and three wrong-result bugs

## 1. A statement that runs out of memory fails; the node stays up

When a generated operator ran out of region memory:

- In a row-buffer handler, `sen/OP_dynamic.sen` executed
  `"default_lowMemHandlers" FIND execute`. No word of that name exists;
  `FIND` left a null and `execute` dereferenced it: SIGSEGV in
  `Step::vSenExecute`, and the node died with every session. Now
  `toggle_PdsRowBufferHandler`, as the table-buffer handler and the other
  operators have it. The same undefined name was in the internal-error
  handler of `sen/op__defHandlers.sen` (`default_internalErrorHandlers`).
- The low-memory path sent a low-memory marker, which cancelled the pipeline
  without an error: the client waited for an answer until its own timeout.
  It now fails the statement as any execution error does (`op__execErr`):
  the client gets `EC_LOW_MEMORY; low memory` within seconds.

Seen with TPC-H Q9 and `/optimiser_off_all` on four partitions (it joins all
of lineitem with full rows). Reproduced in a 4GB region: before, SIGSEGV;
after, the error in about 4 s, the node up, the next queries correct.

## 2. Executing an undefined word fails the operator

`Step::vSenExecute` now throws an internal error ("execute: no word (FIND of
an undefined name?)") when the word is null, instead of dereferencing it. A
misspelt handler name costs one statement, not the node.

`Locator.cpp` used the catalogue word before testing it for null; the test
now comes first.

Audit: every `"<name>" FIND` and `find: <name>` in `sen/*.sen`, and every
literal name looked up from C++, was checked against the words defined in
the sen files and registered by `vAddStep` (the dictionary ignores case).
Apart from the two above, all are defined.

## 3. Tests for low memory

Job tags `/test_low_memory_scan` and `/test_low_memory_rows`
(`Steps_Dynamic.cpp`) make the generated handler of a table buffer or a row
buffer report low memory without running. `Regression/lowMemory.test` checks
that the statement fails with `EC_LOW_MEMORY` and the next one runs, for a
scan, a GROUP BY, a join and an anti join.

## 4. Wrong results

### Queries on replicated tables only

Every partition holds the whole of a replicated table (nation, region).
`select count(*) from nation` returned 25 times the number of partitions; so
did any query whose rows came from replicated tables only. A join from a
replicated table to a partitioned table was right: each partition joins its
copy with its own rows.

`Card_Deck::vMarkReplicatedScanOnce` (run-time optimiser, always on) walks
the partition-level operators: when the stream starts with a scan of a
replicated table and meets no join to a partitioned table before it leaves
the partitions, the scan is marked. The generated table-buffer or index-scan
handler of that stage then returns at once in every partition but the first
of the grid (`CombinedSrc::vScanOnce`, `Dynamic_T::m_iOpLglPtn`; a replicated
table's buffers carry partition 0 everywhere, so the operator's partition
decides).

Not handled: a semi, anti or outer join from such a stream to a partitioned
table (`select ... from nation where exists (select * from supplier ...)`).
Those still return each row once per partition. They need the join to see
all partitions of the joined table.

### NOT EXISTS with a NULL key

The anti join dropped an outer row whose key is NULL. That is right for
`NOT IN` (`NULL NOT IN (...)` is not true) but not for `NOT EXISTS` (the
sub-query finds nothing, so the row is kept). Both compile to the same
`KeyNotExists` operator. `XF_FLATTEN::vInto_KEYnotEXISTS` now records the
NOT EXISTS ones; the operator text carries `NULL_PASSES`, and the generated
NULL check sends such a row on to the next operator
(`CombinedSrc::vNullPasses`).

### Left outer join on the join columns only

`Card_TableList` keeps the inner table of an outer join from heading the
join order (`P_NotFirst`), but only recognised an outer join with an extra ON
condition. One on the join columns alone has just its `LEFT_OUTER` member;
the cost-based order put the inner table first and the rows of the outer
table without a match were lost.

Tests: `Regression/nullKeyJoin.test`, `Regression/replicatedScan.test`.
