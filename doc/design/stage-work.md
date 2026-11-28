# Less work per row inside a stage

Four changes that remove work the stages of a query repeated or did not need.
They came out of the per-stage handler times of the TPC-H SF1 queries
(`/stageTimes`), which showed that the time of the multi-join and large
GROUP BY queries sits in one or two stages each, not in the hand-over between
stages.

## 1. One merge of the groups on one node

A GROUP BY that runs in the partitions is merged twice: on each node (one row
per group leaves the node instead of one per group and partition) and at the
origin. On a cluster of one node the node merge saves no traffic and the
origin repeats it on the same groups. TPC-H Q10 has 38,000 groups with long
string keys (customer name, address, phone, comment): they were merged,
written to a tub and read back twice.

`Card_Deck::bDropNodeMerge_gbyAgg` (run-time optimiser, after
`bLocalise_gbyAgg`) drops the node-level GBYAGG card when the cluster has one
node and both the node and the origin card merge the groups of the partition
card column by column (`bMergesGroupsOf`, the test `bLocalise_gbyAgg` uses).
The origin then reads the rows of the partitions; they have the layout of the
node's rows. The SQL compiler's plan (EXPLAIN) is unchanged.

Job tag `/optimiser_off_single_node_gbyagg` keeps the node merge.

## 2. Column cache for joins whose row leaves out columns

`CombinedSrc::vColCacheJoin` replaces the decoding of a joined row by one load
per column from the column cache of the row's buffer. It gave up when a column
of the joined table had no destination cell, which is the case when the
projection kept in the join stage leaves the column out of the stage's row
(`l_orderkey` in the join of lineitem in Q5 and Q8). Such a column is stepped
over; it is now treated like any other skipped column.

Joins that were decoded row by row and now read the cache (SF1): lineitem in
Q5, Q8 and Q18, orders in Q18, partsupp, supplier and nation in Q9, customer
in Q5 and Q10, supplier in Q8.

## 3. Semi and anti joins

A semi or anti join (EXISTS / NOT EXISTS) decodes the joined row only to
evaluate its constraint; no column of that row leaves the join. It decoded all
of them: Q21 decoded the 16 columns of every candidate lineitem row, comment
included, to compare `l_suppkey` and two dates.

The join is now registered for `vPruneInflate` (columns the constraint does
not name are stepped over) and `vColCacheJoin` (the others come from the
column cache). `vColCacheJoin` reads each column's return label from the
column's own jump, because a semi or anti join numbers the labels from 1.

Job tag `/optimiser_off_semi_join_prune`: as before.

## 4. HAVING in the traversal of the groups

Q18 groups lineitem by `l_orderkey`: 1.5 million groups were written to the
tub and read back by the next stage, whose filter (`sum(l_quantity) > 300`)
kept 57.

`CombinedSrc::bFilterInEos` runs on the code of two consecutive stages
(`Stencil::eSynthesise`, like `bPruneTub`): when stage A ends in a GROUP BY
whose EOS handler writes one row per group, and the first operator of stage B
is a filter on those rows, the filter's expression is copied into A's
traversal in front of the allocation of the row, with each cell of B's row
replaced by the expression A stores in that cell. A group that fails the
filter gets no row. B keeps its filter. Only for filters on fixed-width cells
that A stores with a plain assignment.

Job tag `/optimiser_off_eos_filter`: as before.

## What was tried and dropped

- Keeping the groups of an integer key as ascending runs instead of in the
  hash table (the lines of an order are stored together and in key order).
  The loader fills several buffers at a time, so the key ranges of the buffers
  of a partition overlap; the first buffer change falls back to the hash
  table.
- Transparent huge pages for the engine's heap allocations
  (`glibc.malloc.hugetlb`): no huge pages were granted on the test machine and
  the queries were not faster.
- A hash join pre-filter that tests `l_suppkey = ps_suppkey` on the stored
  rows of a key before the row is fetched (Q9): no measurable gain, the
  rejected pairs are cheap.

## Test

`seth/tests/tests1N4P/Regression/execStage.test` runs each case with and
without its job tag.
