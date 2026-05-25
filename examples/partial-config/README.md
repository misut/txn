# Partial Mode Example

Demonstrates `txn::Mode::Partial` for loading overlay configs on top of C++ default member initializers. Missing non-optional fields keep the C++ defaults instead of erroring.

## Run

```sh
cd examples/partial-config
mise exec -- exon sync
mise exec -- exon build
mise exec -- exon run
```
