document.addEventListener('DOMContentLoaded', function() {
    const issuesList = document.getElementById('issues-container');
    const issueModal = new bootstrap.Modal(document.getElementById('issueModal'));
    const issueModalBody = document.getElementById('issueModalBody');
    const issueModalLabel = document.getElementById('issueModalLabel');
    const viewOnGithubBtn = document.getElementById('viewOnGithub');

    // GitHub API endpoint for your repository's issues
    const apiUrl = 'https://api.github.com/repos/LizardByte/roadmap/issues?state=open&labels=planned&per_page=100';

    fetch(apiUrl)
        .then(response => {
            if (!response.ok) {
                throw new Error('Network response was not ok');
            }
            return response.json();
        })
        .then(issues => {
            if (issues.length === 0) {
                issuesList.innerHTML = '<p>No roadmap items found.</p>';
                return;
            }

            // Clear loading message
            issuesList.innerHTML = '';

            issues.forEach(issue => {
                const issueCol = document.createElement('div');
                issueCol.className = 'col-lg-4 mb-5';

                const issueEl = document.createElement('div');
                issueEl.className = 'card h-100 shadow border-0 rounded-0';
                issueEl.style.cursor = 'pointer';

                // Store issue data for modal
                issueEl.dataset.issue = JSON.stringify(issue);

                // Click event to show modal
                issueEl.addEventListener('click', function() {
                    const issueData = JSON.parse(this.dataset.issue);
                    displayIssueInModal(issueData);
                });

                const cardBody = document.createElement('div');
                cardBody.className = 'card-body text-white p-4 rounded-0';

                // Issue title
                const titleEl = document.createElement('h5');
                titleEl.className = 'card-title mb-3 fw-bolder crowdin-ignore';
                const titleText = document.createElement('span');
                titleText.textContent = issue.title;
                titleText.className = 'text-decoration-none';
                titleEl.appendChild(titleText);

                // Issue metadata
                const metaEl = document.createElement('div');
                metaEl.className = 'd-flex justify-content-between mb-2 small';

                // Issue number
                const numberEl = document.createElement('span');
                numberEl.className = 'badge bg-secondary crowdin-ignore';
                numberEl.textContent = `#${issue.number}`;

                // Issue date
                const dateEl = document.createElement('span');
                dateEl.className = 'crowdin-ignore';
                const createdDate = new Date(issue.created_at);
                dateEl.textContent = createdDate.toLocaleDateString();

                metaEl.appendChild(numberEl);
                metaEl.appendChild(dateEl);

                // Labels
                const labelsEl = document.createElement('div');
                labelsEl.className = 'd-flex flex-wrap gap-1 mb-2';

                // Sort labels alphabetically by name
                const sortedLabels = [...issue.labels].sort((a, b) =>
                    a.name.toLowerCase().localeCompare(b.name.toLowerCase())
                );

                sortedLabels.forEach(label => {
                    const labelEl = document.createElement('span');
                    labelEl.className = 'badge crowdin-ignore';
                    labelEl.textContent = label.name;
                    labelEl.style.backgroundColor = `#${label.color}`;

                    // Determine if label text should be dark or light based on background
                    const r = Number.parseInt(label.color.substring(0, 2), 16);
                    const g = Number.parseInt(label.color.substring(2, 4), 16);
                    const b = Number.parseInt(label.color.substring(4, 6), 16);
                    const brightness = (r * 299 + g * 587 + b * 114) / 1000;
                    labelEl.style.color = brightness > 125 ? '#000' : '#fff';

                    labelsEl.appendChild(labelEl);
                });

                // Add all elements to the card body
                cardBody.appendChild(titleEl);
                cardBody.appendChild(metaEl);
                cardBody.appendChild(labelsEl);

                issueEl.appendChild(cardBody);
                issueCol.appendChild(issueEl);
                issuesList.appendChild(issueCol);
            });

        })
        .catch(error => {
            issuesList.innerHTML = `<p class="text-danger">Error loading roadmap items: ${error.message}</p>`;
        });

    // Function to display issue in modal
    function displayIssueInModal(issue) {
        // Set modal title
        issueModalLabel.textContent = `${issue.title} (#${issue.number})`;

        // Set GitHub link
        viewOnGithubBtn.href = issue.html_url;

        // Create modal content
        let modalContent = document.createElement('div');

        // Issue metadata
        const metaEl = document.createElement('div');
        metaEl.className = 'd-flex justify-content-between mb-3';

        // Issue created date
        const createdEl = document.createElement('span');
        const createdLabel = document.createElement('strong');
        createdLabel.textContent = 'Created:';
        createdEl.appendChild(createdLabel);
        createdEl.appendChild(document.createTextNode(' '));

        const createdValue = document.createElement('span');
        createdValue.className = 'crowdin-ignore';
        const createdDate = new Date(issue.created_at);
        createdValue.textContent = createdDate.toLocaleDateString();
        createdEl.appendChild(createdValue);

        // Issue author
        const authorEl = document.createElement('span');
        const authorLabel = document.createElement('strong');
        authorLabel.textContent = 'By:';
        authorEl.appendChild(authorLabel);
        authorEl.appendChild(document.createTextNode(' '));

        const authorValue = document.createElement('span');
        authorValue.className = 'crowdin-ignore';
        authorValue.textContent = issue.user.login;
        authorEl.appendChild(authorValue);

        metaEl.appendChild(createdEl);
        metaEl.appendChild(authorEl);
        modalContent.appendChild(metaEl);

        // Labels
        if (issue.labels && issue.labels.length > 0) {
            const labelsContainer = document.createElement('div');
            labelsContainer.className = 'mb-3';

            const labelsTitle = document.createElement('strong');
            labelsTitle.textContent = 'Labels:';
            labelsContainer.appendChild(labelsTitle);

            const labelsEl = document.createElement('div');
            labelsEl.className = 'd-flex flex-wrap gap-1 mt-1';

            issue.labels.forEach(label => {
                const labelEl = document.createElement('span');
                labelEl.className = 'badge crowdin-ignore';
                labelEl.textContent = label.name;
                labelEl.style.backgroundColor = `#${label.color}`;

                // Determine if label text should be dark or light based on background
                const r = Number.parseInt(label.color.substring(0, 2), 16);
                const g = Number.parseInt(label.color.substring(2, 4), 16);
                const b = Number.parseInt(label.color.substring(4, 6), 16);
                const brightness = (r * 299 + g * 587 + b * 114) / 1000;
                labelEl.style.color = brightness > 125 ? '#000' : '#fff';

                labelsEl.appendChild(labelEl);
            });

            labelsContainer.appendChild(labelsEl);
            modalContent.appendChild(labelsContainer);
        }

        // Issue body
        const bodyContainer = document.createElement('div');
        bodyContainer.className = 'mt-3 p-3 rounded-0';

        if (issue.body) {
            // Create a container with crowdin-ignore class for the entire rendered content
            const markdownContainer = document.createElement('div');
            markdownContainer.className = 'crowdin-ignore';

            // Use Marked to parse the markdown
            markdownContainer.innerHTML = marked.parse(issue.body);

            // Add the rendered content to the body container
            bodyContainer.appendChild(markdownContainer);
        } else {
            const noContentMsg = document.createElement('div');
            noContentMsg.textContent = 'No description provided.';
            bodyContainer.appendChild(noContentMsg);
        }

        modalContent.appendChild(bodyContainer);

        // Set modal content
        issueModalBody.innerHTML = '';
        issueModalBody.appendChild(modalContent);

        // Show modal
        issueModal.show();
    }
});
