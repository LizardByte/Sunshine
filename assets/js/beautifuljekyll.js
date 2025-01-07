// Dean Attali / Beautiful Jekyll 2023

let BeautifulJekyllJS = {

  bigImgEl: null,
  numImgs: null,

  init: function() {
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
