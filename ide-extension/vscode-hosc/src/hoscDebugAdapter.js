const cp = require('child_process');
const path = require('path');

let seq = 1;
let buffer = Buffer.alloc(0);
let currentProcess = null;
let started = false;
let pendingStart = false;
let processStatus = 'idle';
let launchConfig = {};

function writeMessage(message) {
  const json = JSON.stringify(message);
  const payload = Buffer.from(json, 'utf8');
  const header = Buffer.from(`Content-Length: ${payload.length}\r\n\r\n`, 'utf8');
  process.stdout.write(Buffer.concat([header, payload]));
}

function sendEvent(event, body = {}) {
  writeMessage({ seq: seq++, type: 'event', event, body });
}

function sendResponse(request, body = {}) {
  writeMessage({
    seq: seq++,
    type: 'response',
    request_seq: request.seq,
    command: request.command,
    success: true,
    body
  });
}

function sendErrorResponse(request, message) {
  writeMessage({
    seq: seq++,
    type: 'response',
    request_seq: request.seq,
    command: request.command,
    success: false,
    message,
    body: {
      error: { id: 1, format: message }
    }
  });
}

function sendOutput(category, text) {
  sendEvent('output', { category, output: text.endsWith('\n') ? text : `${text}\n` });
}

function startProgram() {
  if (started) return;

  const program = launchConfig.program;
  if (!program) {
    sendOutput('stderr', 'Missing launch.program');
    sendEvent('terminated', {});
    return;
  }

  const cliPath = launchConfig.cliPath || 'hosc';
  const cwd = launchConfig.cwd || path.dirname(program);

  started = true;
  processStatus = 'running';
  currentProcess = cp.spawn(cliPath, ['run', program], {
    cwd,
    stdio: ['ignore', 'pipe', 'pipe'],
    shell: false,
    windowsHide: false
  });

  currentProcess.stdout.on('data', (chunk) => {
    sendOutput('stdout', chunk.toString('utf8'));
  });

  currentProcess.stderr.on('data', (chunk) => {
    sendOutput('stderr', chunk.toString('utf8'));
  });

  currentProcess.on('error', (err) => {
    processStatus = 'error';
    sendOutput('stderr', `Failed to start process: ${err.message}`);
    sendEvent('exited', { exitCode: 1 });
    sendEvent('terminated', {});
  });

  currentProcess.on('exit', (code) => {
    processStatus = 'exited';
    sendEvent('exited', { exitCode: code ?? 0 });
    sendEvent('terminated', {});
    currentProcess = null;
    started = false;
  });
}

function terminateProgram() {
  if (currentProcess && !currentProcess.killed) {
    currentProcess.kill();
  }
  started = false;
  pendingStart = false;
}

function handleRequest(request) {
  switch (request.command) {
    case 'initialize': {
      sendResponse(request, {
        supportsConfigurationDoneRequest: true,
        supportsTerminateRequest: true,
        supportsEvaluateForHovers: true,
        supportsSetVariable: false,
        supportsStepBack: false,
        supportsFunctionBreakpoints: false,
        supportsConditionalBreakpoints: false
      });
      sendEvent('initialized', {});
      break;
    }

    case 'launch': {
      launchConfig = request.arguments || {};
      pendingStart = !!launchConfig.stopOnEntry;
      processStatus = pendingStart ? 'stopped' : 'idle';
      sendResponse(request, {});
      break;
    }

    case 'setBreakpoints': {
      const bps = (request.arguments?.breakpoints || []).map((bp) => ({
        verified: false,
        line: bp.line
      }));
      sendResponse(request, { breakpoints: bps });
      break;
    }

    case 'setExceptionBreakpoints': {
      sendResponse(request, { breakpoints: [] });
      break;
    }

    case 'configurationDone': {
      sendResponse(request, {});
      if (pendingStart) {
        sendEvent('stopped', { reason: 'entry', threadId: 1, allThreadsStopped: true });
      } else {
        startProgram();
      }
      break;
    }

    case 'threads': {
      sendResponse(request, {
        threads: [{ id: 1, name: 'main' }]
      });
      break;
    }

    case 'stackTrace': {
      const sourcePath = launchConfig.program || '';
      sendResponse(request, {
        stackFrames: [
          {
            id: 1,
            name: path.basename(sourcePath || 'main'),
            line: 1,
            column: 1,
            source: sourcePath ? { name: path.basename(sourcePath), path: sourcePath } : undefined
          }
        ],
        totalFrames: 1
      });
      break;
    }

    case 'scopes': {
      sendResponse(request, {
        scopes: [
          {
            name: 'Runtime',
            variablesReference: 1,
            expensive: false
          }
        ]
      });
      break;
    }

    case 'variables': {
      sendResponse(request, {
        variables: [
          { name: 'status', value: processStatus, variablesReference: 0 },
          { name: 'program', value: launchConfig.program || '', variablesReference: 0 },
          { name: 'cliPath', value: launchConfig.cliPath || 'hosc', variablesReference: 0 }
        ]
      });
      break;
    }

    case 'continue':
    case 'next':
    case 'stepIn':
    case 'stepOut': {
      if (!started) {
        pendingStart = false;
        startProgram();
      }
      sendResponse(request, { allThreadsContinued: true });
      break;
    }

    case 'pause': {
      processStatus = 'stopped';
      sendResponse(request, {});
      sendEvent('stopped', { reason: 'pause', threadId: 1, allThreadsStopped: true });
      break;
    }

    case 'evaluate': {
      sendResponse(request, {
        result: processStatus,
        variablesReference: 0
      });
      break;
    }

    case 'terminate':
    case 'disconnect': {
      terminateProgram();
      sendResponse(request, {});
      sendEvent('terminated', {});
      break;
    }

    default: {
      sendResponse(request, {});
      break;
    }
  }
}

function processIncoming() {
  while (true) {
    const headerEnd = buffer.indexOf('\r\n\r\n');
    if (headerEnd === -1) return;

    const headerText = buffer.slice(0, headerEnd).toString('utf8');
    const match = headerText.match(/Content-Length:\s*(\d+)/i);
    if (!match) {
      buffer = Buffer.alloc(0);
      return;
    }

    const length = Number(match[1]);
    const bodyStart = headerEnd + 4;
    const bodyEnd = bodyStart + length;
    if (buffer.length < bodyEnd) return;

    const body = buffer.slice(bodyStart, bodyEnd).toString('utf8');
    buffer = buffer.slice(bodyEnd);

    let message;
    try {
      message = JSON.parse(body);
    } catch {
      continue;
    }

    if (message && message.type === 'request') {
      try {
        handleRequest(message);
      } catch (err) {
        sendErrorResponse(message, err.message || 'Internal debugger error');
      }
    }
  }
}

process.stdin.on('data', (chunk) => {
  buffer = Buffer.concat([buffer, chunk]);
  processIncoming();
});

process.stdin.resume();