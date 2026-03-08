# Create a stable Sunshine release

Pre-releases in Sunshine are created automatically on every push event to the `master` branch. These are required
to be created before making a stable release. Below are the instructions for converting a pre-release to stable.

1. Wait for the pre-release to be created.
2. Once the pre-release is created, the copr build will begin in the
   [beta copr repo](https://copr.fedorainfracloud.org/coprs/lizardbyte/beta/).
   Wait for this build to succeed before continuing. You can view the status
   [here](https://github.com/LizardByte/Sunshine/actions/workflows/ci-copr.yml?query=event%3Arelease)
3. Once the workflow mentioned in step 2 completes, it will update the GitHub release with the RPM files from the copr
   build.
4. At this point, the GitHub release can be edited.

   - Add any top-level release notes.
   - Ensure any security fixes are mentioned first with links to the security advisories.
   - Following security advisories, breaking changes should be mentioned. Be sure to mention anything the user may need
     to do to ensure a smooth upgrade experience.
   - Then highlight any notable new features and/or fixes.
   - Lastly, reduce the automated changelog list. Things like dependency updates and ci updates can mostly be removed.

5. When saving, uncheck "pre-release" and save the release. This will make the release stable and kick off a series of
   workflows, including but not limited to the following:

   - Create a blog post in [LizardByte.github.io repo](https://github.com/LizardByte/LizardByte.github.io/pulls) via PR

     - Merging this PR will trigger automations that send the blog post link to:

       - LizardByte Discord
       - r/LizardByte subreddit
       - Twitter
       - Facebook

   - Update changelog in [changelog](https://github.com/LizardByte/Sunshine/tree/changelog) branch
   - Update docs on [Read The Docs](https://app.readthedocs.org/projects/sunshinestream/)
   - Update official [Flathub repo](https://github.com/flathub/dev.lizardbyte.app.Sunshine/pulls) via a PR
     (we have merge control)
   - Update our homebrew-homebrew repo via a PR (https://github.com/LizardByte/homebrew-homebrew/pulls)
   - Update our pacman-repo via a PR (https://github.com/LizardByte/pacman-repo/pulls)
   - Update official
     [Winget repo](https://github.com/microsoft/winget-pkgs/issues?q=is%3Apr%20is%3Aopen%20author%3ALizardByte-bot)
     via a PR (we DO NOT have merge control)
   - Build the new version in [stable copr repo](https://copr.fedorainfracloud.org/coprs/lizardbyte/stable/)
   - Send release notification to Moonlight Discord server
