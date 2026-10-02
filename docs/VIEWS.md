# Reference views

Views in the explorer that are useful for debugging and comparing changes.
Open one by appending its hash to the explorer's address (for example
`http://localhost:5173/` with `yarn dev`). Seeding is in the hash where it
matters; without it, the explorer uses its default (random walk).

| view | hash | why it is useful |
|---|---|---|
| Antenna minibrots | `#re=1.4&im=0&span=0.0148875` | Deep minibrots with a fainter one behind them. The random walk draws both; Newton seeding with boundary starts only drew the deep ones. This view exposed the seed bias ("The look changes: seed bias" in [`research/SEEDING_AND_ZOOM.md`](research/SEEDING_AND_ZOOM.md)). Compare seeding modes here. |
| Near 1.217, wide | `#re=1.2170756143656&im=0&span=0.4756&seeding=newton` | To confirm with the user. |
| Near 1.217, at the proposed span limit | `#re=1.217075&im=0&span=0.005&seeding=newton` | To confirm with the user. Same centre as above at span 0.005, the limit proposed for the precision artifacts in [`ISSUES.md`](ISSUES.md). |

Add a view with its hash and a line on what it shows. The hash format is
described under "URI settings" in the research doc.
