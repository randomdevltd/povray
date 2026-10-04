# Performance work

- Target the current `performance` branch for speed work. Use a separate branch and PR for each coherent change, and keep stacked changes based on their recorded fork points.
- Profile the phase and feature being changed before choosing an algorithm. Include preparation or map-building cost, trace cost, memory, and any work moved between phases.
- Use existing public scenes for ordinary behavior and regression checks. Add small public scenes that stress the expensive feature, including realistic combinations of media, photons, lighting, and geometry. A large integrated scene is useful as a final check, but it is not a substitute for focused measurements.
- Prefer user-space instruction counts with a matching parse-only run and repeated, alternating builds. Record whether counters were multiplexed. Treat CPU and wall times as supporting evidence, especially for small differences on a busy machine.
- Run resource-intensive builds and renders through the scheduler named in the local workspace guidance. Give short build installs priority over long background renders. Use `+PR` for renders; if anti-aliasing is needed, use method 4 (`+AM4`).
- Check decoded images as well as speed. An exact-path optimization should preserve pixels. A new approximate mode should be judged against a high-quality reference for detail, noise, and artifacts, along with how easily users can tune it. Faster, cleaner results with fewer controls are more valuable than matching the old algorithm's errors.
- A well-measured gain in a narrow but realistic case is useful. Report the conditions that produce it and check ordinary scenes for regressions. Look for costs that multiply when effects are combined or sample counts rise.
- Keep legacy scene versions compatible. New defaults and simplified controls may be gated by `#version 4.0`; explicit settings should continue to override defaults. New transport features may belong only to the newer mode.
- Implement closely related performance opportunities within the current work when they can be measured and reviewed together. Propose a separate ticket for another subsystem or a substantial redesign.
- Keep public code, fixtures, documentation, commits, and PR text about the code and reproducible public examples. Do not include private workload names or paths, board references, account details, or machine security settings.
