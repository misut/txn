# Custom Parser Hook Example

Demonstrates the powerful ADL customization point `txn_from_value(txn::tag<T>, ...)` allowing a type (Color) to accept alternative shapes such as hex strings "#rrggbb" while still supporting the normal reflected object form.

## Run

```sh
cd examples/custom-hook
mise exec -- exon sync
mise exec -- exon build
mise exec -- exon run
```
