/**
 * Update stable and prerelease links with release data from GitHub.
 *
 * @param {object} options Release display options.
 * @param {string} options.repository GitHub repository in owner/name form.
 * @param {string} options.stableButtonSelector Selector for the stable release link.
 * @param {string} options.stableVersionSelector Selector for the stable version text.
 * @param {string} options.prereleaseButtonSelector Selector for the prerelease link.
 * @param {string} options.prereleaseVersionSelector Selector for the prerelease version text.
 * @returns {Promise<void>} Promise fulfilled after the release display is updated.
 */
async function updateGitHubReleaseVersions(options) {
    const repositoryPattern = /^[A-Za-z0-9_.-]+\/[A-Za-z0-9_.-]+$/u;
    const [owner, repository] = options.repository.split('/');
    const invalidSegments = new Set(['.', '..']);
    if (
        !repositoryPattern.test(options.repository)
        || invalidSegments.has(owner)
        || invalidSegments.has(repository)
    ) {
        throw new TypeError('repository must use the owner/name format');
    }

    const stableButton = document.querySelector(options.stableButtonSelector);
    const stableVersion = document.querySelector(options.stableVersionSelector);
    const prereleaseButton = document.querySelector(options.prereleaseButtonSelector);
    const prereleaseVersion = document.querySelector(options.prereleaseVersionSelector);
    if (!stableButton || !stableVersion || !prereleaseButton || !prereleaseVersion) {
        throw new TypeError('release display selectors must match existing elements');
    }

    const apiUrl = `https://api.github.com/repos/${encodeURIComponent(owner)}/${encodeURIComponent(repository)}/releases`;
    const response = await fetch(apiUrl);
    if (!response.ok) {
        throw new Error(`GitHub releases request failed with status ${response.status}`);
    }

    const releases = await response.json();
    if (!Array.isArray(releases)) {
        throw new TypeError('GitHub releases response must be an array');
    }

    const latestStableRelease = releases.find(release => !release.prerelease);
    stableButton.classList.toggle('d-none', !latestStableRelease);
    if (!latestStableRelease) {
        prereleaseButton.classList.add('d-none');
        return;
    }

    stableButton.href = latestStableRelease.html_url;
    stableVersion.textContent = latestStableRelease.tag_name;

    const latestPrerelease = releases.find(release => release.prerelease);
    const prereleaseIsNewer = latestPrerelease
        && new Date(latestPrerelease.published_at) > new Date(latestStableRelease.published_at);
    prereleaseButton.classList.toggle('d-none', !prereleaseIsNewer);
    if (prereleaseIsNewer) {
        prereleaseButton.href = latestPrerelease.html_url;
        prereleaseVersion.textContent = latestPrerelease.tag_name;
    }
}

globalThis.updateGitHubReleaseVersions = updateGitHubReleaseVersions;
