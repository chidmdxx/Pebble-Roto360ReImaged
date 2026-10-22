# Security policy

## Supported versions

Security fixes target the latest release and `main`. Older releases are not
maintained separately.

## Report a vulnerability

Use [Report a vulnerability](https://github.com/chidmdxx/Pebble-Roto360ReImaged/security/advisories/new)
to send a private report. Do not publish exploit details, credentials or device identifiers in issues or pull requests.
Include the affected version, impact, and a minimal reproduction using synthetic
data. There is no guaranteed response time or bug bounty.

If a credential has been exposed, revoke or rotate it first. Removing it from
the current tree does not remove it from history, forks, caches, or downloads.

## Maintainer access and releases

The repository owner is the only human with write/admin access. Public
contributors can fork the repository and propose changes, but cannot merge
them or publish releases.

GitHub rulesets, not this file or CODEOWNERS, enforce the following policy:

- Only the owner can update `main`. All changes, including the owner's, require
  a squash-merged pull request, resolved review conversations, and successful `secrets`,
  `tests`, and `build` checks from GitHub Actions against the current base.
- Nobody bypasses those PR/CI requirements, force-pushes `main`, or deletes it.
  Required review approvals remain zero so a solo owner can merge their own PR.
  CODEOWNERS requests the owner's review; it is not an authorization mechanism.
  Merge commits and rebase merges are disabled in repository settings and in
  the main ruleset.
- Only the owner can create tags. Existing tags cannot be updated or deleted.
- Releases remain manual. Publish only a reviewed, successful `main` commit.
  Attach and inspect the PBW in a draft before publishing: release immutability
  locks the published assets and tag. Release titles and notes remain editable.
  Do not distribute PR artifacts as trusted releases.

GitHub has no separate release-publisher role on this personal repository:
release access follows repository write/contents permissions. Tag protection
does not prevent a contents-write token from creating a release for an existing
tag. Keep collaborators read-only, Actions tokens read-only, and audit installed
apps, deploy keys, OAuth grants, and personal access tokens.

Copilot does not receive a permanent ruleset bypass or release token. Copilot
using the owner's credentials has the owner's authority and must act only on
the owner's explicit instruction. GitHub cannot distinguish that automation
from the owner or enforce the meaning of a chat instruction. A separately
installed app with contents-write permissions is a separate trust decision.

## Automation and publication checklist

- Keep Actions pinned to full commit SHAs and limited to GitHub-authored actions.
  Dependabot proposes Actions updates; the owner reviews and merges them.
- Require approval for workflows from every external fork contributor. Review
  the entire PR before approving execution. Do not use self-hosted runners for
  public PRs or run untrusted PR code in a privileged `pull_request_target` job.
- Do not grant workflows `contents: write`, PR-approval permissions, publishing
  credentials, or broad secrets. Checkout credentials must not persist.
  The repository's read-only token default is not a ceiling: a trusted workflow
  can explicitly request write access. Review workflow changes accordingly.
- Keep Dependabot alerts/security updates, private vulnerability reporting,
  secret scanning, and push protection enabled where supported. Review alerts;
  scanning is not proof that every secret or vulnerability has been found.
- The required `secrets` job runs checksum-pinned Gitleaks on pushes and PRs,
  including fork PRs after workflow approval. It scans fetched Git history, not
  just the final diff, so deleting a secret in a later commit does not hide it.
  Findings fail the check and log output redacts secret values. The job uses
  built-in rules with an empty ignore file, ignoring PR-supplied config,
  fingerprints, and inline allow comments. Review any edits to the workflow
  itself: like other PR-defined CI, it is not tamper-proof against workflow edits.
  Gitleaks does not upload the repository to a scanning service. Dependabot
  checks vulnerable dependencies and updates actions; it does not find secrets.
  Update the pinned Gitleaks version and checksum together after reviewing its
  release; Dependabot does not update this downloaded binary.
- The phone-side JavaScript provides only an embedded offline settings page
  and validates the seconds mode and daily step goal before sending them to the
  watch. It makes no network requests and collects no account, watch or location
  data. Step totals are read locally through Pebble Health and never sent to
  the phone or a server; sleep data is not read. Its tests,
  the native harness and compiler checks are not a security scan.
  Review the pinned Pebble CLI/SDK and their transitive Python
  dependencies separately; Actions Dependabot does not maintain those tools.
- Use a passkey or hardware-backed 2FA on the owner account, store recovery
  codes offline, and use narrowly scoped, expiring tokens. These are account
  settings, not repository settings.
- Before publishing, inspect reachable history, branches/tags, issues, PR
  attachments, release assets, Actions artifacts/logs, and app screenshots for
  private material. Public copies may remain after a visibility reversal.

Review [repository rules](https://github.com/chidmdxx/Pebble-Roto360ReImaged/settings/rules),
[Actions settings](https://github.com/chidmdxx/Pebble-Roto360ReImaged/settings/actions),
[collaborators](https://github.com/chidmdxx/Pebble-Roto360ReImaged/settings/access),
and [security settings](https://github.com/chidmdxx/Pebble-Roto360ReImaged/settings/security_analysis)
after changing access or installing an integration. GitHub settings live outside
Git: copying these files to another repository does not enable the controls.
