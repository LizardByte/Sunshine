function generateExamples(endpoint, method, body = null) {
  let curlBodyString = '';
  let curlHeaderString = '';
  let psBodyString = '';
  let psContentTypeString = '';
  let psBodyParams = '';

  if (body) {
    const curlJsonString = JSON.stringify(body).replaceAll('"', String.raw`\"`);
    curlBodyString = ` -d "${curlJsonString}"`;
    curlHeaderString = ' -H "Content-Type: application/json"';
    psBodyString = `-Body (ConvertTo-Json ${JSON.stringify(body)})`;
    psContentTypeString = '-ContentType \'application/json\'';
    psBodyParams = ' `\n  ' + psBodyString + ' `\n  ' + psContentTypeString;
  }

  return {
    cURL: `curl -u user:pass${curlHeaderString} -X ${method.trim()} -k https://localhost:47990${endpoint.trim()}${curlBodyString}`,
    Python: `import json
import requests
from requests.auth import HTTPBasicAuth

requests.${method.trim().toLowerCase()}(
    auth=HTTPBasicAuth('user', 'pass'),
    url='https://localhost:47990${endpoint.trim()}',
    verify=False,${body ? `\n    json=${JSON.stringify(body)},` : ''}
).json()`,
    JavaScript: `fetch('https://localhost:47990${endpoint.trim()}', {
  method: '${method.trim()}',
  headers: {
    'Authorization': 'Basic ' + btoa('user:pass'),${body ? `\n    'Content-Type': 'application/json',` : ''}
  }${body ? `,\n  body: JSON.stringify(${JSON.stringify(body)}),` : ''}
})
.then(response => response.json())
.then(data => console.log(data));`,
    PowerShell: `Invoke-RestMethod \`
  -SkipCertificateCheck \`
  -Uri 'https://localhost:47990${endpoint.trim()}' \`
  -Method ${method.trim()} \`
  -Headers @{
    Authorization = 'Basic ' + [Convert]::ToBase64String([Text.Encoding]::ASCII.GetBytes('user:pass'))
  }${psBodyParams}`
  };
}

function escapeHtml(value) {
  return value
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#039;');
}

function createTabs(examples) {
  const languages = {
    cURL: 'bash',
    Python: 'python',
    JavaScript: 'javascript',
    PowerShell: 'powershell'
  };
  const tabs = Object.entries(examples).map(([label, example]) => `
    <li>
      <strong class="dockle-tab-title">${escapeHtml(label)}</strong>
      <pre data-dockle-language="${languages[label]}"><code>${escapeHtml(example)}</code></pre>
    </li>`).join('');

  return `<div class="dockle-tabs dockle-tabs-alias" data-dockle-tab-group="api-language"><ul>${tabs}</ul></div>`;
}
