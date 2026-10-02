# 0002. The Metropolis sampler optimises for the look, not an unbiased estimate

**Status:** Accepted
**Date:** 2026-09-28

## Context

Buddha++'s Metropolis sampler (`buddha_generator::metropolis()`) is a
Metropolis–Hastings chain over starting points with target `L² · C`, where
every valid proposal is drawn once, accepted or not, with no reweighting. That
is not a correct Buddhabrot estimator: the image is biased toward long orbits
that cross the window. It is also what gives the renders their character:
deep iterations, high-contrast minibrots outside the main set, and zooms that
do not waste samples.

Moving Metropolis to the GPU and speeding it up on the CPU raised the
question of whether to fix the bias along the way. Source:
[`plans/METROPOLIS_PLAN.md`](../plans/METROPOLIS_PLAN.md), "Goal",
"Non-goals" and "What defines the look".

## Decision

The sampler is judged by the images it produces, not by convergence to the
Buddhabrot. An unbiased Buddhabrot is the naive sampler's job.

- Rules 1 to 4 of the plan (target, symmetric proposal, drawing every valid
  proposal, chain length) are kept because they define the look. The target
  became `L^a · C^b` with both exponents as settings (`8113824`).
- Anything else may change freely as long as images look similar: random
  numbers, float precision, seeding, lane structure.

## Consequences

- Choices such as a Hastings correction for an asymmetric mutation are
  questions of look, not correctness (research doc section 2).
- Seeding changes the image, because short local chains show where seeds come
  from. Newton seeding (MR !3) is evaluated by eye against walk seeding, and
  its seed bias is measured against uniform seeds
  ([`research/SEEDING_AND_ZOOM.md`](../research/SEEDING_AND_ZOOM.md), "The
  look changes").
- Metropolis and naive renders cannot be compared or merged as estimates of
  the same quantity. Checkpoints record the sampler and refuse a mismatch;
  blending is planned as separate histogram layers mixed at output time.
- Alternatives that change the look were tried and dropped on those grounds,
  e.g. holding-time weights (6.7× fewer draws, but "too contrasted",
  [session notes](../notes/SESSION_NOTES_2026-09.md)).
