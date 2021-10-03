A barebone [LSP] implementation.

Dependencies:

```
sudo apt install libabsl-dev nlohmann-json3-dev
```

So far implemented

  * Parsing [json-rpc] input stream, which is encapsulated in Header/Body
    parts according to [LSP] and dispatching it to RPCHandlers and
    NotificationHandlers.
  * The handlers are type-safe with C++ structs for ease of use within
    the application, using [jcxxgen].
  * Text buffers are updated with the
    `didOpen`, `didChange`, `didSave`, `didSave` events.
  * Prepared calling of linting etc. in idle time.
  * No RPC handlers implemented yet, but prepared to add type-safe hooks.

[LSP]: https://microsoft.github.io/language-server-protocol/specifications/specification-current/
[json-rpc]: https://www.jsonrpc.org/specification
[jcxxgen]: https://github.com/hzeller/jcxxgen
