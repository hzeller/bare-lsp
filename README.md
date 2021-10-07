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
sudo apt install nlohmann-json3-dev libabsl-dev libgtest-dev libgmock-dev
```

The [abseil] dependency is minimal (some string manipulation and `absl::Status`)
and it would be trivial to replace it with other similar library functionality
whatever is commonly used in the project to be integrated in.

The code generation for structs convertible to json is done using [jcxxgen].

## Hooking up to editor

This will be specific to your editor. The main thing is that you need to tell
it to start your language server binary (here: `lsp-server`) in a particular
language environment.

### Emacs
Here a simple example how to hook up emacs; put this in your `~/.emacs` file
and make sure the binary is in your `$PATH` (or use full path).

```lisp
(require 'lsp-mode)
(add-to-list 'lsp-language-id-configuration '(text-mode . "text"))
(lsp-register-client
 (make-lsp-client :new-connection (lsp-stdio-connection "/path/to/my/lsp-server")
                  :major-modes '(text-mode)
                  :server-id 'txt-ls))

(add-hook 'text-mode-hook 'lsp)
```

The hovering feature will be shown in the minibuffer with each move of the
cursor.

### Vim

TBD. What I found so far, there is
[vim-lsp](https://github.com/prabirshrestha/vim-lsp) that can be used.

Start out with adding plug if not already.
```
curl -fLo ~/.vim/autoload/plug.vim --create-dirs \
   https://raw.githubusercontent.com/junegunn/vim-plug/master/plug.vim
```

This seems to be the starting point
```
cat >> ~/.vim/vimrc <<EOF
call plug#begin('~/.vim/plugged')
Plug 'prabirshrestha/vim-lsp'
Plug 'mattn/vim-lsp-settings'
call plug#end()
EOF
```

.. then somewhere on here https://github.com/mattn/vim-lsp-settings there should
be an explanation how to add your own server (help needed for this section,
what needs to be configured for a new server ?)

### Sublime
Consult https://lsp.readthedocs.io/

After enabling the package control you can install 'LSP' with it.
Go to `Preferences > Package Settings > LSP > Settings`

Configure settings so that your language server is started in the scope
of language you need.

```json
// Settings in here override those in "LSP/LSP.sublime-settings"
{
  "clients": {
    "bare-lsp": {
      "command": ["my-lsp-server"],
      "enabled": true,
      "languageId": "text",
      "scopes": ["text.plain"],
      "syntaxes": [ "Packages/Text/Plain text.tmLanguage"]
    }
  }
}
```

The hovering feature in this demo bare-lsp shows up when double-clicking a
word, then hover over the text.

There is a `Tools > LSP > Troubleshoot Server Configuration` which might
be helpful.

### Kate

https://docs.kde.org/trunk5/en/kate/kate/kate-application-plugin-lspclient.html

For our example, it seems that kate does not consider 'text' a separate
language, so let's configure that in markdown.

First, enable LSP by checking `Settings > Configure Kate > Plugins > LSP Client`
Then, there is a new `{} LSP Client` icon appearing on the left of the configure dialog. In the _User Server Settings_ tab, enter the lsp server configuration
to get it started on a particular language. Here: for `markdown`

```json
{
    "servers": {
        "markdown": {
            "command": ["my-lsp-server"],
            "root": "",
            "url": "https://github.com/hzeller/bare-lsp"
        }
    }
}
```

## Debugging

For debugging the protocol, it is useful to log what is going on between your
editor and the lsp server. The [bidi-tee] is a useful debugging tool - it does
bidirectional piping between processes and logs the data in a file that can
be examined later. You could hook it up by writing a little shell-script like
the one below and call that from your editor.

```bash
#!/bin/bash

DATE_SUFFIX=$(date +"%Y-%m-%d_%H%M")
/usr/local/bin/bidi-tee /tmp/mylsp-${DATE_SUFFIX}.log -- /path/to/my/lsp-server $@
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
  * Demo implementation of RPC handlers
     - Initialization (`initialize`, `initialized`)
     - Sample hover command (`textDocument/hover`)
       _(in demo: showing character count in word)_
     - Sample formatting command (`textDocument/formatting` and
       `textDocument/rangeFormatting`) _(centering text)_
     - Sample 'diagnostics' that mark all sequences `wrong` to be wrong :)
     - codeAction: Provide alternative fixes to a problem.
     - Highlight: all words that are the same under the cursor are marked.
  * Prepared calling of linting etc. in idle time.

Pro-tip: Useful for testing and replaying sessions is the [bidi-tee] tool.

[LSP]: https://microsoft.github.io/language-server-protocol/specifications/specification-current/
[nlohmann/json]: https://github.com/nlohmann/json
[abseil]: https://abseil.io/
[json-rpc]: https://www.jsonrpc.org/specification
[jcxxgen]: https://github.com/hzeller/jcxxgen
[bidi-tee]: https://github.com/hzeller/bidi-tee
