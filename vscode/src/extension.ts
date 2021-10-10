import * as vscode from 'vscode';
import * as vscodelc from 'vscode-languageclient/node';

// Global object to dispose of previous language clients.
let client: undefined | vscodelc.LanguageClient = undefined;

function initLanguageClient() {
	const config = vscode.workspace.getConfiguration('languageBareLSP');
	const serverPath: string = config.get('serverPath') as string;

	const clangd: vscodelc.Executable = {
        command: serverPath
    };

    const serverOptions: vscodelc.ServerOptions = clangd;

    // Options to control the language client
    const clientOptions: vscodelc.LanguageClientOptions = {
        // Register the server for plain text documents
        documentSelector: [{ scheme: 'file', language: 'plaintext' }]
    };

    // Create the language client and start the client.
    client = new vscodelc.LanguageClient(
        'languageBareLSP',
        'Bare LSP example server',
        serverOptions,
        clientOptions
    );
    client.start();
}

// VSCode entrypoint to bootstrap an extension
export function activate(_: vscode.ExtensionContext) {
	// If a configuration change even it fired, let's dispose
	// of the previous client and create a new one.
	vscode.workspace.onDidChangeConfiguration((event) => {
		if (!event.affectsConfiguration('languageBareLSP')) {
			return;
		}
		if (!client) {
           return initLanguageClient(); 
        }
        client.stop().finally(() => {
            initLanguageClient();
        });
	});
	return initLanguageClient();
}

// Entrypoint to tear it down.
export function deactivate(): Thenable<void> | undefined {
    if (!client) {
        return undefined;
    }
	return client.stop();
}
