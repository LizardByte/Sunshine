// projects section script

// get project container
let container = document.getElementById("project-container")
let org_name = "LizardByte"
let base_url = `https://app.${org_name.toLowerCase()}.dev`
let cache_repo = "dashboard"


function shouldProcessRepo(repo) {
    if (repo['archived'] === true) return false;
    if (repo['description'] === null) return false;
    return !(repo['fork'] === true && !repo['topics'].includes("featured-fork"));
}

function createBannerElement(repo) {
    let banner_div = document.createElement("div")
    banner_div.className = "hover-zoom"

    let banner_link = document.createElement("a")
    banner_link.className = "crowdin-ignore"
    banner_link.href = repo['html_url']
    banner_link.target = "_blank"
    banner_div.append(banner_link)

    let banner = document.createElement("img")
    banner.className = "card-img-top rounded-0"
    banner.src = `${base_url}/${cache_repo}/github/openGraphImages/${repo['name']}_624x312.png`
    banner.alt = ""
    banner_link.append(banner)

    return { banner_div, banner_link }
}

function createCardBody(repo) {
    let card_body = document.createElement("div")
    card_body.className = "card-body p-4 rounded-0"

    let card_title_link = document.createElement("a")
    card_title_link.className = "text-decoration-none project-card-link crowdin-ignore"
    card_title_link.href = repo['html_url']
    card_title_link.target = "_blank"
    card_body.appendChild(card_title_link)

    let card_title_text = document.createElement("h5")
    card_title_text.className = "card-title mb-3 fw-bolder crowdin-ignore"
    card_title_text.textContent = repo['name']

    if (repo['fork'] === true && repo['topics'].includes("featured-fork")) {
        let fork_badge = document.createElement("span")
        fork_badge.className = "badge bg-info ms-2"
        fork_badge.textContent = "Fork"
        card_title_text.appendChild(fork_badge)
    }

    card_title_link.appendChild(card_title_text)

    let card_paragraph = document.createElement("p")
    card_paragraph.className = "card-text mb-0"
    card_paragraph.textContent = repo['description']
    card_body.appendChild(card_paragraph)

    return { card_body, card_title_link }
}

function loadCommitActivity(repo, card_footer) {
    $.ajax({
        url: `${base_url}/${cache_repo}/github/commitActivity/${repo['name']}.json`,
        type: "GET",
        dataType: "json",
        success: function (commitActivity) {
            let activity_container = document.createElement("div")
            activity_container.className = "commit-activity-graph mt-2 mb-2 mx-3"
            activity_container.style.cssText = "display: flex; gap: 2px; height: 32px; align-items: flex-end;"

            let maxCommits = Math.max(...commitActivity.map(week => week.total))

            for (let week of commitActivity) {
                let bar = document.createElement("div")
                let height = maxCommits > 0 ? (week.total / maxCommits) * 100 : 0
                let intensity = 0.3 + (height / 100) * 0.7

                bar.className = week.total === 0 ? 'commit-bar-empty crowdin-ignore' : 'commit-bar-active crowdin-ignore'
                bar.style.cssText = `
                    flex: 1;
                    min-width: 2px;
                    height: ${Math.max(height, 10)}%;
                    border-radius: 1px;
                    transition: all 0.2s ease;
                    opacity: ${week.total === 0 ? 1 : intensity};
                `
                bar.title = `${week.total} commits this week`
                activity_container.appendChild(bar)
            }

            card_footer.insertBefore(activity_container, card_footer.firstChild)
        },
        error: function() {
            console.log(`No commit activity data available for ${repo['name']}`)
        }
    })
}

function createRepoDataRow(repo, card_footer, banner_link, card_title_link) {
    let repo_data_row = document.createElement("div")
    repo_data_row.className = "d-flex align-items-center"
    card_footer.appendChild(repo_data_row)

    let github_link = document.createElement("a")
    github_link.className = "nav-link project-nav-link ms-3"
    github_link.href = repo['html_url']
    github_link.target = "_blank"
    repo_data_row.appendChild(github_link)

    let github_link_image = document.createElement("i")
    github_link_image.className = "fa-fw fa-brands fa-github"
    github_link.prepend(github_link_image)

    $.ajax({
        url: `${base_url}/${repo['name']}/`,
        type: "GET",
        success: function () {
            banner_link.href = `${base_url}/${repo['name']}/`
            card_title_link.href = `${base_url}/${repo['name']}/`
        },
    })

    let star_link = document.createElement("a")
    star_link.className = "nav-link nav-link-sm project-nav-link ms-3 crowdin-ignore"
    star_link.href = `https://star-history.com/#${repo['full_name']}`
    star_link.target = "_blank"
    star_link.textContent = globalThis.formatNumber(repo['stargazers_count'])
    repo_data_row.appendChild(star_link)

    let star_link_image = document.createElement("i")
    star_link_image.className = "fa-fw fa-solid fa-star"
    star_link.prepend(star_link_image)

    let fork_link = document.createElement("a")
    fork_link.className = "nav-link nav-link-sm project-nav-link ms-3 crowdin-ignore"
    fork_link.href = `https://github.com/${repo['full_name']}/network/members`
    fork_link.target = "_blank"
    fork_link.textContent = globalThis.formatNumber(repo['forks'])
    repo_data_row.appendChild(fork_link)

    let fork_link_image = document.createElement("i")
    fork_link_image.className = "fa-fw fa-solid fa-code-fork"
    fork_link.prepend(fork_link_image)

    return repo_data_row
}

function addDocsLink(repo, readthedocs, repo_data_row) {
    for (let docs in readthedocs) {
        let docs_repo = readthedocs[docs]['repository']['url'].toLowerCase();
        let project_repo = repo['clone_url'].toLowerCase();

        if (docs_repo !== project_repo) continue;

        let docs_url = readthedocs[docs]['urls']['documentation']
        try {
            let parsedUrl = new URL(docs_url);
            if (parsedUrl.host !== "docs.lizardbyte.dev") continue;
        } catch (e) {
            console.error("Invalid URL:", docs_url, e);
            continue;
        }

        let docs_link = document.createElement("a")
        docs_link.className = "nav-link ms-3"
        docs_link.href = docs_url
        docs_link.target = "_blank"
        repo_data_row.appendChild(docs_link)

        let docs_link_image = document.createElement("img")
        docs_link_image.className = "readthedocs-icon"
        docs_link_image.src = "https://cdn.jsdelivr.net/npm/simple-icons@v15/icons/readthedocs.svg"
        docs_link_image.alt = "ReadTheDocs"
        docs_link_image.title = "ReadTheDocs"
        docs_link_image.style.width = "24px"
        docs_link_image.style.height = "24px"
        docs_link.prepend(docs_link_image)
    }
}

function loadLanguageIcons(repo, card_footer) {
    $.ajax({
        url: `${base_url}/${cache_repo}/github/languages/${repo['name']}.json`,
        type: "GET",
        dataType: "json",
        success: function (languages) {
            let language_data_row = document.createElement("div")
            language_data_row.className = "card-group p-3 align-items-center"
            card_footer.appendChild(language_data_row)

            for (let language in languages) {
                let language_file = encodeURIComponent(`${language}.svg`)
                let language_icon = document.createElement("img")
                language_icon.className = "language-logo crowdin-ignore"
                language_icon.src = `${base_url}/uno/language-icons/${language_file}`
                language_icon.alt = language
                language_icon.title = language
                language_data_row.append(language_icon)
            }
        }
    });
}

function buildRepoCard(repo, readthedocs) {
    let column = document.createElement("div")
    column.className = "col-lg-4 mb-5"
    container.appendChild(column)

    let card = document.createElement("div")
    card.className = "card h-100 shadow border-0 rounded-0"
    column.appendChild(card)

    let { banner_div, banner_link } = createBannerElement(repo)
    card.append(banner_div)

    let { card_body, card_title_link } = createCardBody(repo)
    card.appendChild(card_body)

    let card_footer = document.createElement("div")
    card_footer.className = "card-footer p-2 pt-0 border-0 rounded-0"
    card.appendChild(card_footer)

    loadCommitActivity(repo, card_footer)

    let repo_data_row = createRepoDataRow(repo, card_footer, banner_link, card_title_link)
    addDocsLink(repo, readthedocs, repo_data_row)
    loadLanguageIcons(repo, card_footer)
}


// create project cards
$(document).ready(function(){
    // Set cache = false for all jquery ajax requests.
    $.ajaxSetup({
        cache: false,
    });

    // get readthedocs projects
    let readthedocs = []
    $.ajax({
        url: `${base_url}/${cache_repo}/readthedocs/projects.json`,
        type: "GET",
        dataType: "json",
        success: function (data) {
            for (let item in data) {
                readthedocs.push(data[item])
            }
        }
    });

    $.ajax({
        url: `${base_url}/${cache_repo}/github/repos.json`,
        type: "GET",
        dataType:"json",
        success: function (result) {
            let sorted = result.sort(globalThis.rankingSorter("stargazers_count", "name"))

            for(let repo in sorted) {
                if (shouldProcessRepo(sorted[repo])) {
                    buildRepoCard(sorted[repo], readthedocs)
                }
            }
        }
    });
});
