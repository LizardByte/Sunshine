function generateExamples(endpoint, method, body = null) {
  let curlBodyString = '';
  let psBodyString = '';

  if (body) {
    const curlJsonString = JSON.stringify(body).replace(/"/g, '\\"');
    curlBodyString = ` -d "${curlJsonString}"`;
    psBodyString = `-Body (ConvertTo-Json ${JSON.stringify(body)})`;
  }

  return {
    cURL: `curl -u user:pass -H "Content-Type: application/json" -X ${method.trim()} -k https://localhost:47990${endpoint.trim()}${curlBodyString}`,
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
    'Authorization': 'Basic ' + btoa('user:pass'),
    'Content-Type': 'application/json',
  }${body ? `,\n  body: JSON.stringify(${JSON.stringify(body)}),` : ''}
})
.then(response => response.json())
.then(data => console.log(data));`,
    PowerShell: `Invoke-RestMethod \`
  -SkipCertificateCheck \`
  -ContentType 'application/json' \`
  -Uri 'https://localhost:47990${endpoint.trim()}' \`
  -Method ${method.trim()} \`
  -Headers @{Authorization = 'Basic ' + [Convert]::ToBase64String([Text.Encoding]::ASCII.GetBytes('user:pass'))}
  ${psBodyString}`
  };
}

function hashString(str) {
  let hash = 0;
  for (let i = 0; i < str.length; i++) {
    const char = str.charCodeAt(i);
    hash = (hash << 5) - hash + char;
    hash |= 0; // Convert to 32bit integer
  }
  return hash;
}

function createTabs(examples) {
  const languages = Object.keys(examples);
  let tabs = '<div class="tabs-overview-container"><div class="tabs-overview">';
  let content = '<div class="tab-content">';

  languages.forEach((lang, index) => {
    const hash = hashString(examples[lang]);
    tabs += `<button class="tab-button ${index === 0 ? 'active' : ''}" onclick="openTab(event, '${lang}')"><b class="tab-title" title=" ${lang} "> ${lang} </b></button>`;
    content += `<div id="${lang}" class="tabcontent" style="display: ${index === 0 ? 'block' : 'none'};">
                  <div class="doxygen-awesome-fragment-wrapper">
                    <div class="fragment">
                      ${examples[lang].split('\n').map(line => `<div class="line">${line}</div>`).join('')}
                    </div>
                    <doxygen-awesome-fragment-copy-button id="copy-button-${lang}-${hash}" title="Copy to clipboard">
                      <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24">
                        <path d="M0 0h24v24H0V0z" fill="none"></path>
                        <path d="M16 1H4c-1.1 0-2 .9-2 2v14h2V3h12V1zm3 4H8c-1.1 0-2 .9-2 2v14c0 1.1.9 2 2 2h11c1.1 0 2-.9 2-2V7c0-1.1-.9-2-2-2zm0 16H8V7h11v14z"></path>
                      </svg>
                    </doxygen-awesome-fragment-copy-button>
                  </div>
                </div>`;
  });

  tabs += '</div></div>';
  content += '</div>';

  setTimeout(() => {
    languages.forEach((lang, index) => {
      const hash = hashString(examples[lang]);
      const copyButton = document.getElementById(`copy-button-${lang}-${hash}`);
      copyButton.addEventListener('click', copyContent);
    });
  }, 0);

  return tabs + content;
}

function copyContent() {
  const content = this.previousElementSibling.cloneNode(true);
  if (content instanceof Element) {
    // filter out line number from file listings
    content.querySelectorAll(".lineno, .ttc").forEach((node) => {
      node.remove();
    });
    let textContent = Array.from(content.querySelectorAll('.line'))
      .map(line => line.innerText)
      .join('\n')
      .trim(); // Join lines with newline characters and trim leading/trailing whitespace
    navigator.clipboard.writeText(textContent);
    this.classList.add("success");
    this.innerHTML = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24"><path d="M0 0h24v24H0V0z" fill="none"/><path d="M9 16.17L4.83 12l-1.42 1.41L9 19 21 7l-1.41-1.41L9 16.17z"/></svg>`;
    window.setTimeout(() => {
      this.classList.remove("success");
      this.innerHTML = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24"><path d="M0 0h24v24H0V0z" fill="none"/><path d="M16 1H4c-1.1 0-2 .9-2 2v14h2V3h12V1zm3 4H8c-1.1 0-2 .9-2 2v14c0 1.1.9 2 2 2h11c1.1 0 2-.9 2-2V7c0-1.1-.9-2-2-2zm0 16H8V7h11v14z"/></svg>`;
    }, 980);
  } else {
    console.error('Failed to copy: content is not a DOM element');
  }
}

function openTab(evt, lang) {
  const tabcontent = document.getElementsByClassName("tabcontent");
  for (const content of tabcontent) {
    content.style.display = "none";
  }

  const tablinks = document.getElementsByClassName("tab-button");
  for (const link of tablinks) {
    link.className = link.className.replace(" active", "");
  }

  const selectedTabs = document.querySelectorAll(`#${lang}`);
  for (const tab of selectedTabs) {
    tab.style.display = "block";
  }

  const selectedButtons = document.querySelectorAll(`.tab-button[onclick*="${lang}"]`);
  for (const button of selectedButtons) {
    button.className += " active";
  }
}
