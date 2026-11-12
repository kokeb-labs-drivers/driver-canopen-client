# Knowledge index

What to read or run, and when. Skills are in `.claude/skills/`, the agent in `.claude/agents/`.

| Item | Use it when |
| --- | --- |
| [Recurring defects](recurring-defects.md) | You review, fix or design anything: the defect classes (RD-nn) that reached reviewers or CI, with the check for each |
| [Mechanization policy](mechanization-policy.md) | You find a defect, add a check, or decide whether a finding becomes a lint, a test or a review question; the pre-push gate and the roles |
| `review-round` skill | You organise or run an independent review round: inputs, mutant check of fixes, claims versus code, stop rule |
| `safety-reviewer` agent | You want one read-only review of a change or a whole driver, ranked BLOCKER, MAJOR, minor |
| `hardware-lessons-review` skill | You write or review protocol, start-up, setters, switch-off, buffers, state guards, models, fuzz |
| `datasheet-conformance` skill | You write a register map, timing or error handling, or review a change against the datasheet |
| `misra-review` skill | A change touches `inc/ src/ tests/ examples/`; the MISRA patterns that failed CI |
| `cpp20-profile` skill | You write or review any C++; the KB-CPP20 table and its review questions |
| `verify` skill | Before every push: all CI checks on your machine |
| `first-push` skill | A new driver is about to be pushed for the first time: rehearsal, review rounds, evidence |
| `add-driver-feature`, `add-port` skills | A new chip capability; a new platform under `examples/` |
| `new-driver-from-template` skill | Turning a repository made from the template into a driver |
| `release`, `bump-pin` skills | A version release; a submodule or workflow pin update |
| [Testing and CI](testing.md) | What the tests, fuzz and gates check and how to run them |

Order for a new driver: build with `new-driver-from-template`, `datasheet-conformance` and `add-driver-feature`;
host tests and `verify`; the gate (see the policy); `review-round` until the stop rule holds; `first-push`; `release`.

These three pages and the skills are managed by the template: `scaffold.py --check-drift --fix` ships them to every
driver, `_config/mkdocs.yml` keeps the pages out of the site navigation (`not_in_nav`), and the skills link to them
by relative path, so the links resolve in the repository.
