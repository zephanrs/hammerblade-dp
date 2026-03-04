# chaining variants

- `original/`: the existing ring-forwarding implementation
- `direct/`: direct multicast between lanes with local inbox waits
- `tree/`: original-style schedule with a tree broadcast per iteration

Use `APP=chaining` for `direct`, `APP=chaining_original` for `original`, and `APP=chaining_tree` for `tree`.
