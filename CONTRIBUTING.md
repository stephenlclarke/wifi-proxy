# Contributing To wifi-proxy

Thank you for helping improve `wifi-proxy`. Contributions are easiest to review when they are focused, tested, documented, and safe for protected branches.

## Pull Requests

Use pull requests for all non-trivial changes.

1. Create a topic branch from the default branch.
2. Keep each pull request focused on one bug fix, feature, or documentation update.
3. Use Conventional Commits for commit messages and pull request titles.
4. Sign every commit with a GitHub-supported signature method such as SSH or GPG.
5. Add or update tests for behaviour changes where the repository has a test suite.
6. Update documentation when behaviour, configuration, installation, or developer workflow changes.
7. Remove credentials, tokens, private keys, personal data, and private environment details from code, tests, logs, and screenshots.

Maintainers review pull requests before merge. Direct pushes to protected branches should be limited to maintainers and automation that has passed the required checks.

## Conventional Commits

Use this format:

```text
type(scope): short imperative summary
```

Common types include `feat`, `fix`, `docs`, `test`, `refactor`, `ci`, and `chore`.

## Quality Bar

Run the repository's local validation before requesting review. Prefer the `Makefile` target when the repository provides one, because that keeps local and CI checks aligned.

Every behaviour change should include an appropriate test or a clear note explaining why automated coverage is not practical. Keep changes small enough that reviewers can understand the risk.

## Protected Branch Safety

These guardrails keep the repository safe for outside contributions:

- Require pull requests before merging to protected branches.
- Require maintainer review for external contributors.
- Keep CI permissions as narrow as possible.
- Avoid running untrusted pull request code with write-scoped secrets.
- Prefer dependency updates through Dependabot or similarly reviewed automation.

## Code Of Conduct

Contributors are expected to follow [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).
