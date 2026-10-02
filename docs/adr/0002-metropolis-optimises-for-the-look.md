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
question of whether to fix the bias along the way. Source: the Metropolis
plan (`METROPOLIS_PLAN.md`, in git history), "Goal", "Non-goals" and "What
defines the look".

The look comes from five rules of the original sampler:

1. **Target.** `f(c) = L² · C`, where `L` is the orbit's escape iteration and
   `C` the number of orbit points inside the window. Periodic, excluded or
   non-contributing proposals (`C = 0`) are skipped.
2. **Proposal.** `c' = c + polar(Exp(2) · U · 20 / scale, U · 2π)`: isotropic,
   so symmetric, with a radius of about 20 pixels at any zoom.
3. **Drawing.** Every non-skipped proposal is drawn once, whether or not it is
   accepted. Rejected steps do not redraw the current state, and nothing
   reweights the samples. This is what makes the image differ from both the
   Buddhabrot and `f`.
4. **Chain length.** `max(256 · C₀, 2 · L₀)` proposals, from the seed's `C₀`
   and `L₀`.
5. **Seeding.** A Gaussian random walk (step `|N(0,1)|`) from the origin,
   keeping the first point with `C > 0`, giving up after 256 steps. Seeds are
   not drawn.

## Decision

The sampler is judged by the images it produces, not by convergence to the
Buddhabrot. An unbiased Buddhabrot is the naive sampler's job.

- Rules 1 to 4 (target, symmetric proposal, drawing every valid proposal,
  chain length) are kept because they define the look. The target
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
  e.g. holding-time weights: drawing each accepted state weighted by how long
  the chain stays there gave 6.7× fewer orbit draws and a 1.40× speedup, but
  the image looked too contrasted (local branch `exp/mh-holding-weights`,
  September 2026).
- Rule 5 turned out to be part of the look too: Newton seeding (MR !3, now
  the explorer's default) gives a look closer to uniform seeding, less deep
  than the walk.
