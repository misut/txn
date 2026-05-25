# Real-World Integration with tomlcpp

This is the "real consumer" story: using txn together with its primary ValueLike provider `tomlcpp`.

Parses a TOML document into a reflected C++ struct using `toml::parse` + `txn::from_value`.

## Run

```sh
cd examples/with-tomlcpp
mise exec -- exon sync
mise exec -- exon build
mise exec -- exon run
```
