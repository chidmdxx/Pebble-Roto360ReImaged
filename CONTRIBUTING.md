# Contributing

Fork the repository, make a focused change, and open a pull request against `main`. Only the repository owner merges changes and publishes releases. Contributions are under the project's [Apache 2.0 license](LICENSE).

Follow the [build instructions](README.md#build-and-install) and the [Pebble development guidance](.github/skills/pebble-development/SKILL.md). Use Node.js 22 or later and GCC or Clang for `npm test`; Windows contributors should use WSL. App changes also need a build for all four supported watches. For layout changes, include emulator screenshots of a round watch and a rectangular watch. Do not claim an emulator proves how the face looks on a physical watch.

Contribute only your own work or assets under a compatible license, and record third-party assets in `NOTICE`. This project is an independent remake: do not add code, images, or fonts taken from the original Roto360 or its package.

Never commit SDK profiles, tokens, private keys, or device details. Follow [the security policy](SECURITY.md) for private vulnerability reports.

PRs must pass the `secrets` check as well as the tests and build. The scanner checks Git history, so removing a credential in a follow-up commit is not enough. Revoke exposed credentials and contact the maintainer privately before requesting help with cleanup.

External fork workflows wait for maintainer approval. Passing CI or an automated review is not permission to merge or publish. Copilot must not merge, publish, change access controls, or change repository visibility without an explicit instruction from the owner.
