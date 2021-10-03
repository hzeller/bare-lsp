A barebone LSP implementation
=============================

Starting point for server implementations of the [LSP] protocol. Provides
the infrastructure to easily hook in functionality using modern C++ features.

## Dependencies

This uses [nlohmann/json] for the json parsing and processing and [abseil]
for string utility functions and an error code abstraction.

libabsl-dev is available in current Debian testing, but on other platforms it
might need to be compiled separately.
```
sudo apt install nlohmann-json3-dev libabsl-dev
```

The [abseil] dependency is minimal (some string manipulation and `absl::Status`)
and it would be trivial to replace it with other similar library functionality
if they are more common in the project to be used in.

The code generation for structs convertible to json is done using [jcxxgen].

### Hooking up to editor

This will be specific to your editor. The main thing is that you need to tell
it to start your language server binary (here: `lsp-server`) in a particular
language environment.

Here a simple example how to hook up emacs; put this in your `~/.emacs` file
and make sure the binary is in your path (or use full path).

```lisp
(require 'lsp-mode)
(add-to-list 'lsp-language-id-configuration '(text-mode . "text"))
(lsp-register-client
 (make-lsp-client :new-connection (lsp-stdio-connection "lsp-server")
                  :major-modes '(text-mode)
                  :server-id 'txt-ls))

(add-hook 'text-mode-hook 'lsp)
```

For debugging the protocol, it is useful to log what is going on between your
editor and the lsp server. The [bidi-tee] is a useful debugging tool - it does
bidirectional piping between processes and logs the data in a file that can
be examined later.

```
#!/bin/bash

DATE_SUFFIX=$(date +"%Y-%m-%d_%H%M")
/usr/local/bin/bidi-tee /tmp/mylsp-${DATE_SUFFIX}.log -- ~/bare-lsp/lsp-server $@
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
[nlohmann/json]: https://github.com/nlohmann/json
[abseil]: https://abseil.io/
[json-rpc]: https://www.jsonrpc.org/specification
[jcxxgen]: https://github.com/hzeller/jcxxgen
[bidi-tee]: https://github.com/hzeller/bidi-tee
