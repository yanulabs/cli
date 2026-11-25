# Security Policy

## Supported versions

Security fixes are applied to the latest stable release line.

| Version | Supported |
|---|---|
| 1.2.x | Yes |
| Earlier than 1.2 | No |

## Reporting a vulnerability

Please do **not** report suspected vulnerabilities in a public issue.

Use GitHub private vulnerability reporting:

https://github.com/yanulabs/cli/security/advisories/new

While the repository remains private, use the private contact options published
on the [Yanu Labs GitHub organization](https://github.com/yanulabs) instead. The
GitHub reporting link becomes available when the repository is made public.

Include, when possible:

- A clear description of the vulnerability and its impact.
- The affected `cli.hpp` version or commit.
- A minimal reproduction, including parser schema and exact arguments.
- Compiler, standard library, operating system, and build flags.
- Whether the issue affects parsing, config/response-file access, completion
  generation, or another subsystem.

## Handling and disclosure

- Reports are reviewed by the Yanu Labs maintainers.
- We aim to acknowledge a report within seven days.
- We may ask for additional reproduction details or a proposed fix.
- Public disclosure should be coordinated with the maintainers after a fix or
  mitigation is available.

For non-security conduct concerns, follow the reporting process in the
[Code of Conduct](CODE_OF_CONDUCT.md).
