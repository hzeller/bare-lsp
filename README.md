A barebone LSP implementation
=============================

Starting point for server implementations of the [LSP] protocol. Provides
the infrastructure to easily hook in functionality using modern C++ features.

## Dependencies

```
sudo apt install libabsl-dev nlohmann-json3-dev
```

## Features
So far implemented

  * Parsing [json-rpc] input stream, which is encapsulated in Header/Body
    parts according to [LSP] and dispatching it to RPCHandlers and
    NotificationHandlers.
  * Schema is [described in yaml](./lsp-protocol.yaml) from which structs
    are code generated using [jcxxgen]. This allows for implementing
    type-safe RPC handlers.
  * Text document event handlers are implemented (`didOpen`, `didChange`,
    `didSave`, `didSave`). These take care of applying the edit-events to
    track the buffer content, ready to be used for language services.
  * Prepared calling of linting etc. in idle time.
  * No RPC handlers implemented yet, but ready to accept type-safe hooks.

Pro-tip: Useful for testing and replaying sessions is the [bidi-tee] tool.

[LSP]: https://microsoft.github.io/language-server-protocol/specifications/specification-current/
[json-rpc]: https://www.jsonrpc.org/specification
[jcxxgen]: https://github.com/hzeller/jcxxgen
[bidi-tee]: https://github.com/hzeller/bidi-tee
