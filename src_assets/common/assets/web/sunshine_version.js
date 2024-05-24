class SunshineVersion {
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

  parseVersion(version) {
    if (!version) {
      return null;
    }
    let v = version;
    if (v.indexOf("v") === 0) {
      v = v.substring(1);
    }
    return v.split('.').map(Number);
  }

  isGreater(otherVersion) {
    let otherVersionParts;
    if (otherVersion instanceof SunshineVersion) {
      otherVersionParts = otherVersion.versionParts;
    } else if (typeof otherVersion === 'string') {
      otherVersionParts = this.parseVersion(otherVersion);
    } else {
      throw new Error('Invalid argument: otherVersion must be a SunshineVersion object or a version string');
    }

    if (!this.versionParts || !otherVersionParts) {
      return false;
    }
    for (let i = 0; i < Math.min(3, this.versionParts.length, otherVersionParts.length); i++) {
      if (this.versionParts[i] > otherVersionParts[i]) {
        return true;
      }
    }
    return false;
  }
}

export default SunshineVersion;
