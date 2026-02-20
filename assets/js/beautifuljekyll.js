// Dean Attali / Beautiful Jekyll 2023

let BeautifulJekyllJS = {

  bigImgEl: null,
  numImgs: null,

  init: function() {
    // Initialize theme switcher before other components
    BeautifulJekyllJS.initTheme();

    setTimeout(BeautifulJekyllJS.initNavbar, 10);

    let navbar = document.querySelector(".navbar");

    // Shorten the navbar after scrolling a little bit down
    window.addEventListener('scroll', function() {
      if (window.scrollY > 50) {
        navbar.classList.add("top-nav-short");
      } else {
        navbar.classList.remove("top-nav-short");
      }
    });

    // On mobile, hide the avatar when expanding the navbar menu
    document.getElementById('main-navbar').addEventListener('show.bs.collapse', function() {
      navbar.classList.add("top-nav-expanded");
    });
    document.getElementById('main-navbar').addEventListener('hidden.bs.collapse', function() {
      navbar.classList.remove("top-nav-expanded");
    });

    // show the big header image
    BeautifulJekyllJS.initImgs();

    BeautifulJekyllJS.initSearch();
  },

  initTheme: function() {
    // Check if theme switcher is enabled (either dropdown or toggle button should exist)
    const themeDropdown = document.getElementById('themeDropdown');
    const themeToggle = document.getElementById('theme-toggle');
    const isDropdownMode = !!themeDropdown;
    const isButtonMode = !!themeToggle;

    if (!isDropdownMode && !isButtonMode) {
      // Theme switcher not enabled, skip initialization
      return;
    }

    // Get stored theme preference or default to 'auto'
    const getStoredTheme = () => localStorage.getItem('theme');
    const setStoredTheme = theme => localStorage.setItem('theme', theme);

    // Get the preferred theme based on user's selection or system preference
    const getPreferredTheme = () => {
      const storedTheme = getStoredTheme();
      if (storedTheme) {
        return storedTheme;
      }
      return 'auto';
    };

    // Get the actual theme to apply (light or dark), resolving 'auto' to system preference
    const getThemeToApply = (theme) => {
      if (theme === 'auto') {
        return globalThis.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
      }
      return theme;
    };

    // Get the next theme in cycle for button mode (auto -> light -> dark -> auto)
    const getNextTheme = (currentTheme) => {
      if (currentTheme === 'auto') return 'light';
      if (currentTheme === 'light') return 'dark';
      return 'auto';
    };

    // Update the title attribute with current theme
    const updateTitle = (theme) => {
      if (themeToggle) {
        const themeNames = { auto: 'Auto', light: 'Light', dark: 'Dark' };
        themeToggle.setAttribute('title', `Toggle theme (currently: ${themeNames[theme]})`);
      }
    };

    // Set the theme on the document
    const setTheme = (theme) => {
      const themeToApply = getThemeToApply(theme);
      document.documentElement.dataset.bsTheme = themeToApply;

      // Update navbar classes after theme change
      setTimeout(BeautifulJekyllJS.initNavbar, 10);

      // Update active state of theme dropdown items (dropdown mode only)
      if (isDropdownMode) {
        const themeDropdownItems = document.querySelectorAll('[data-bs-theme-value]');
        themeDropdownItems.forEach(item => {
          const itemTheme = item.dataset.bsThemeValue;
          if (itemTheme === theme) {
            item.classList.add('active');
          } else {
            item.classList.remove('active');
          }
        });
      }

      // Update the theme icon in the navbar
      const themeIcon = document.getElementById('theme-icon-dropdown') || document.getElementById('theme-icon-button');
      if (themeIcon) {
        themeIcon.classList.remove('fa-sun', 'fa-moon', 'fa-circle-half-stroke');
        if (theme === 'light') {
          themeIcon.classList.add('fa-sun');
        } else if (theme === 'dark') {
          themeIcon.classList.add('fa-moon');
        } else {
          themeIcon.classList.add('fa-circle-half-stroke');
        }
      }

      // Update title for button mode
      if (isButtonMode) {
        updateTitle(theme);
      }
    };

    // Initialize theme
    setTheme(getPreferredTheme());

    // Listen for system theme changes when in auto mode
    globalThis.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => {
      const storedTheme = getStoredTheme();
      if (storedTheme === 'auto' || !storedTheme) {
        setTheme('auto');
      }
    });

    // Add event listeners based on mode
    if (isDropdownMode) {
      // Dropdown mode: add click listeners to each dropdown item
      const themeDropdownItems = document.querySelectorAll('[data-bs-theme-value]');
      themeDropdownItems.forEach(item => {
        item.addEventListener('click', (e) => {
          e.preventDefault();
          const theme = item.dataset.bsThemeValue;
          setStoredTheme(theme);
          setTheme(theme);
        });
      });
    } else if (isButtonMode) {
      // Button mode: cycle through themes on click
      themeToggle.addEventListener('click', (e) => {
        e.preventDefault();
        const currentTheme = getPreferredTheme();
        const nextTheme = getNextTheme(currentTheme);
        setStoredTheme(nextTheme);
        setTheme(nextTheme);
      });
    }
  },

  initNavbar: function() {
    // Set the navbar-dark/light class based on its background color
    const rgb = getComputedStyle(document.querySelector('.navbar')).backgroundColor.replace(/[^\d,]/g, '').split(",");
    const brightness = Math.round(( // http://www.w3.org/TR/AERT#color-contrast
      parseInt(rgb[0]) * 299 +
      parseInt(rgb[1]) * 587 +
      parseInt(rgb[2]) * 114
    ) / 1000);

    let navbar = document.querySelector(".navbar");
    if (brightness <= 125) {
      navbar.classList.remove("navbar-light");
      navbar.classList.add("navbar-dark");
    } else {
      navbar.classList.remove("navbar-dark");
      navbar.classList.add("navbar-light");
    }
  },

  initImgs: function() {
    // If the page has large images to randomly select from, choose an image
    if (document.getElementById("header-big-imgs")) {
      BeautifulJekyllJS.bigImgEl = document.getElementById("header-big-imgs");
      BeautifulJekyllJS.numImgs = BeautifulJekyllJS.bigImgEl.getAttribute("data-num-img");

      // 2fc73a3a967e97599c9763d05e564189
      // set an initial image
      const imgInfo = BeautifulJekyllJS.getImgInfo();
      const src = imgInfo.src;
      const desc = imgInfo.desc;
      BeautifulJekyllJS.setImg(src, desc);

      // For better UX, prefetch the next image so that it will already be loaded when we want to show it
      const getNextImg = function() {
        const imgInfo = BeautifulJekyllJS.getImgInfo();
        const src = imgInfo.src;
        const desc = imgInfo.desc;

        const prefetchImg = new Image();
        prefetchImg.src = src;
        // if I want to do something once the image is ready: `prefetchImg.onload = function(){}`

        setTimeout(function() {
          const img = document.createElement("div");
          img.className = "big-img-transition";
          img.style.backgroundImage = 'url(' + src + ')';
          document.querySelector(".intro-header.big-img").prepend(img);
          setTimeout(function() { img.style.opacity = "1"; }, 50);

          // after the animation of fading in the new image is done, prefetch the next one
          setTimeout(function() {
            BeautifulJekyllJS.setImg(src, desc);
            img.remove();
            getNextImg();
          }, 1000);
        }, 6000);
      };

      // If there are multiple images, cycle through them
      if (BeautifulJekyllJS.numImgs > 1) {
        getNextImg();
      }
    }
  },

  getImgInfo: function() {
    const randNum = Math.floor((Math.random() * BeautifulJekyllJS.numImgs) + 1);
    const src = BeautifulJekyllJS.bigImgEl.getAttribute("data-img-src-" + randNum);
    const desc = BeautifulJekyllJS.bigImgEl.getAttribute("data-img-desc-" + randNum);

    return {
      src: src,
      desc: desc
    }
  },

  setImg: function(src, desc) {
    document.querySelector(".intro-header.big-img").style.backgroundImage = 'url(' + src + ')';

    let imgDesc = document.querySelector(".img-desc");
    if (typeof desc !== typeof undefined && desc !== false && desc !== null) {
      imgDesc.textContent = desc;
      imgDesc.style.display = "block";
    } else {
      imgDesc.style.display = "none";
    }
  },

  initSearch: function() {
    if (!document.getElementById("beautifuljekyll-search-overlay")) {
      return;
    }

    document.getElementById("nav-search-link").addEventListener('click', function(e) {
      e.preventDefault();
      document.getElementById("beautifuljekyll-search-overlay").style.display = "block";
      const searchInput = document.getElementById("nav-search-input");
      searchInput.focus();
      searchInput.select();
      document.body.classList.add("overflow-hidden");
    });
    document.getElementById("nav-search-exit").addEventListener('click', function(e) {
      e.preventDefault();
      document.getElementById("beautifuljekyll-search-overlay").style.display = "none";
      document.body.classList.remove("overflow-hidden");
    });
    document.addEventListener('keyup', function(e) {
      if (e.key === "Escape") {
        document.getElementById("beautifuljekyll-search-overlay").style.display = "none";
        document.body.classList.remove("overflow-hidden");
      }
    });
  }
};

// 2fc73a3a967e97599c9763d05e564189

document.addEventListener('DOMContentLoaded', BeautifulJekyllJS.init);
