/**
 * Convert Full Changelog URLs to clickable links in GitHub release notes
 */
(function() {
    'use strict';

    function makeChangelogLinksClickable() {
        // Find all paragraphs that contain "Full Changelog" text and a GitHub compare URL
        const paragraphs = document.querySelectorAll('p');

        // Loop through paragraphs in reverse order and break after the first match
        for (let i = paragraphs.length - 1; i >= 0; i--) {
            const paragraph = paragraphs[i];
            const text = paragraph.textContent || paragraph.innerText;

            // Check if this paragraph contains "Full Changelog" and a GitHub compare URL
            if (
                text.includes('Full Changelog') &&
                text.includes('/compare/') &&
                text.includes('…')
            ) {
                // Extract the URL from the text
                const urlMatch = text.match(/(https:\/\/github\.com\/\S+)/);

                if (!urlMatch) {
                    continue;
                }

                const url = urlMatch[1];
                let parsedUrl;
                try {
                    parsedUrl = new URL(url);
                } catch (e) {
                    continue;
                }
                // Only allow https://github.com links with the expected pathname format
                // Pattern: /LizardByte/<repo-name>/compare/<version-a>…<version-b>
                const encodedEllipsis = encodeURI('…')
                const githubOrg = 'LizardByte'
                const pathPattern = new RegExp(`^\\/${githubOrg}\\/\\S+\\/compare\\/\\S+${encodedEllipsis}\\S+$`);
                if (
                    parsedUrl.protocol !== 'https:' ||
                    parsedUrl.hostname !== 'github.com' ||
                    !pathPattern.test(parsedUrl.pathname)
                ) {
                    continue;
                }

                // Safely replace the URL text in the paragraph with a clickable link
                const urlIndex = text.indexOf(url);
                // Clear the paragraph contents
                paragraph.textContent = '';
                // Add text before the URL as a text node
                paragraph.appendChild(document.createTextNode(text.slice(0, urlIndex)));
                // Create the anchor element for the URL
                const link = document.createElement('a');
                link.href = parsedUrl.href;
                link.textContent = decodeURI(parsedUrl.href);
                link.target = '_blank';
                link.rel = 'noopener noreferrer';
                paragraph.appendChild(link);
                // Add text after the URL as a text node
                paragraph.appendChild(document.createTextNode(text.slice(urlIndex + url.length)));

                // Break after first match
                break;
            }
        }
    }

    // Run when DOM is ready
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', makeChangelogLinksClickable);
    } else {
        makeChangelogLinksClickable();
    }
})();
