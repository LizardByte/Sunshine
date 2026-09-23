/**
 * @brief Parsed Sunshine release version and comparison helpers.
 */
class SunshineVersion {
  /**
   * @brief Construct a version from GitHub release data or a version string.
   *
   * @param {?object} release GitHub release data.
   * @param {?string} version Version string when release data is unavailable.
   */
  constructor(release = null, version = null) {
    if (release) {
      this.release = release;
      this.version = release.tag_name;
      this.versionName = release.name;
      this.versionTag = release.tag_tag;
    } else if (version) {
      this.release = null;
      this.version = version;
      this.versionName = null;
      this.versionTag = null;
    } else {
      throw new Error('Either release or version must be provided');
    }
    this.versionParts = this.parseVersion(this.version);
    this.versionMajor = this.versionParts ? this.versionParts[0] : null;
    this.versionMinor = this.versionParts ? this.versionParts[1] : null;
    this.versionPatch = this.versionParts ? this.versionParts[2] : null;
  }

  /**
   * @brief Split a Sunshine version string into numeric components.
   *
   * @param {string} version Version string to parse.
   * @return {?number[]} Numeric version components.
   */
  parseVersion(version) {
    if (!version) {
      return null;
    }
    let v = version;
    if (v.startsWith("v")) {
      v = v.substring(1);
    }
    return v.split('.').map(Number);
  }

  /**
   * @brief Compare this version with another Sunshine version.
   *
   * @param {SunshineVersion|string} otherVersion Version to compare.
   * @return {boolean} Whether this version is greater.
   */
  isGreater(otherVersion) {
    let otherVersionParts;
    if (otherVersion instanceof SunshineVersion) {
      otherVersionParts = otherVersion.versionParts;
    } else if (typeof otherVersion === 'string') {
      otherVersionParts = this.parseVersion(otherVersion);
    } else {
      throw new TypeError('Invalid argument: otherVersion must be a SunshineVersion object or a version string');
    }

    if (!this.versionParts || !otherVersionParts) {
      return false;
    }
    for (let i = 0; i < Math.min(3, this.versionParts.length, otherVersionParts.length); i++) {
      if (this.versionParts[i] !== otherVersionParts[i]) {
        return this.versionParts[i] > otherVersionParts[i];
      }
    }
    return false;
  }
}

export default SunshineVersion;
