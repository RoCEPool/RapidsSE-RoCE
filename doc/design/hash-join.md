# Hash join with a Bloom filter

Status: implemented for inner index joins on one integer key.

## Why

On TPC-H SF1 the join-heavy queries (Q7, Q8, Q9, Q21, Q3, Q5) spent most of
their time in join stages, and most of that descending the joined table's
index once per arriving row (`IndexTreePositionGE`, about 1 us per probe into
lineitem or orders). DuckDB builds a hash table on one side and streams the
other side through it.

## How

An index join that is the first operator of a row handler, inner, on one
integer key, with no operator behind it in the stage that needs the arriving
order or joins again (ordered aggregate, top, limit, sort, another join;
`CombinedSrc::bHashJoinEligible`), is generated as a hash join
(`vAppendCodeHashJoin`, fragment `vHashJoinPosition_int`, store `HashJoin` in
`IndexTree.cpp`):

1. **Stream first.** The first arriving rows, one per 1000 entries of the
   joined index, go straight to the index as in an index join. A join of few
   rows (Q17) stays an index join and keeps streaming to the next stage.
2. **Build.** The other arriving rows are copied (strings too) into a hash
   table keyed by the join key. A stored row is
   `[next row with the key][next stored row][key][cells]`.
3. **Probe, at the end of the input** (EOS handler), `iExtern_HashJoinMode`
   chooses:
   - **walk** when the join's rows are at least one per 150 index entries:
     walk the joined index once in key order; for each entry test a Bloom
     filter of the stored keys (16 bits per key, two hash bits) and only on
     a hit look the key up in the hash table; join the entry with each
     stored row of its key;
   - **replay** otherwise: look each stored row up in the index, as an index
     join does.
   - **scan** in place of the walk when every buffer of the joined table
     partition is a CRW buffer whose key column the column cache holds (no
     deleted rows, within the budget) and their rows are as many as the
     index's entries: read the keys from the cached 8-byte cells, buffer by
     buffer, with the same Bloom filter and hash lookup; a match goes to the
     join's dereference as a one-entry range of a made-up index entry
     (buffer, item, key). A walk spends about 33 ns an entry following the
     index's skip lists (Q7: 3M lineitem entries a partition for 121K rows).

The thresholds come from SF1 measurements: walking beat probing down to one
row per 75 index entries (Q9) and lost at one per 423 (Q19).

Job tags are read when the code is made and passed to the probe as flags:
the stores' externs do not read the tag set on worker threads. (The
le_shortcut tag of the index lookup is now read the same way, once per
`Dynamic`.)

## Switches

- `/optimiser_off_hash_join`: index join as before.
- `/optimiser_off_hash_join_bloom`: walk without the Bloom filter.
- `/optimiser_off_hash_join_scan`: walk the index instead of scanning the
  cached key column.
- `/hash_join_walk`, `/hash_join_replay`: force the probe (tests).
- `/hash_join_trace`: trace rows stored, index entries and the choice.

## Results (local SF1, execute time, mean of 3)

| Query | index join | hash join |
|---|---|---|
| Q9  | 919 ms | 547 ms |
| Q21 | 527 ms | 369 ms |
| Q8  | 575 ms | 414 ms |
| Q7  | 570 ms | 462 ms |
| Q3  | 340 ms | 268 ms |
| Q5  | 302 ms | 290 ms |
| Q10 | 429 ms | 413 ms |

The Bloom filter alone: Q21 426 -> 380, Q7 516 -> 469, Q3 317 -> 287,
Q8 442 -> 417 ms; Q9 (most keys hit) unchanged.

Scan against walk (local SF1, 4 threads, handler time of the stage summed
over partitions, `/stageTimes`): Q7 S06 (lineitem) 190 -> 152 ms, S04
(orders) 121 -> 84 ms; Q8 S05 (lineitem) 222 -> 188 ms.

## Limits and next steps

- String or multi-column keys, joins that are not the first operator of a
  row handler: still index joins. (Outer, semi and anti joins: see below.)
- The scan still reads every key of the joined table; the matched rows are
  then decoded one by one through the column-cache join path.

## Outer, semi and anti joins

The scan above joins each row of the joined table with the stored rows of its
key, in the order of the joined table. That fits an inner join only: an outer
join has to emit the stored rows that found nothing, a semi join each stored
row once, an anti join the rows that found nothing or only rows that fail the
constraint. Their generated code decides this per arriving row, over the
range of index entries of its key.

These joins are now hash joins that only replay (flag 32 of
`iExtern_HashJoinMode`, mode 4):

1. Build as before: the arriving rows are stored. Their strings stay relative
   to their cells, as in a row buffer (`__HJ_REL__`), because the operators
   behind a semi or anti join read the arriving row itself, with the code of a
   row buffer.
2. At the end of the input, one scan of the joined table's cached key column
   (Bloom filter, then the slots of the stored keys) collects the row
   references whose key has stored rows; a counting sort lays them out as
   made-up index entries (`Ent_int`: key and row reference), one range per key
   (`HashJoin::vBuildRanges`).
3. The stored rows are replayed in arrival order through the join's own
   position code. `vExtern_IndexTreePositionGE_int` hands out the range of the
   key from those entries instead of descending the index
   (`bHashJoinRange`); the step to the next entry is `+ 1`. The rest of the
   join (constraint, NULL row of an outer join, the single output of a semi
   join) runs unchanged, on the same ranges an index join would see, except
   for the order of the entries within a key.

The fragment `vHashJoinPosition_int` now ends with the position of the index;
for these joins `vAppendCodeHashJoin` cuts it there and the join's own
position fragment follows. In the replay the row info of the arriving row
points at the stored row, and `m_iJoinedRows` / `m_bProcessRow` are reset as
at the top of the row loop.

Where it does not apply: when the scan cannot be prepared, or with
`/optimiser_off_hash_join_scan`, the replay positions the index (mode 2, as an
inner join with few rows). A table partition below 500,000 rows is joined
through its index from the start and no row is stored (TPC-H Q16 joins 60,000
rows with the 5,000 suppliers of a partition: the index is in the cache);
`/hash_join_replay_small` lifts that limit for tests.
`/optimiser_off_hash_join_replay` turns the whole change off.

Used at SF1 by Q4 (semi, lineitem), Q13 (outer, orders), Q21 (anti, lineitem)
and Q22 (anti, orders).

Test: `Regression/hashJoinReplay.test`. The regression lists were also run
once with the limit at 0 for every query.
