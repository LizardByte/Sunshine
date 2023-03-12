var sunshineUI = {
  init: function() {
    sunshineUI.toggleTheme();
    //document click events
    const btn = document.querySelector("#toggleTheme");
    btn.addEventListener("click", function() {
      sunshineUI.toggleTheme(1);
    });

  },
  toggleTheme: function(click) {
    let themeCookie = getCookie("sunshineTheme");
    let options = document.querySelector('#toggleTheme');
    let btn = options.querySelector('.active');

    if (!click) {
      //init page load
      if (themeCookie != null) {
        //cookie is set, make that button active
        btn = options.querySelectorAll('[data-bs-theme="' + themeCookie + '"]')[0];
      }
    } else {
      let optionCount = getChildNodes(btn).count;
      let optionIndex = getChildNodes(btn).index;
      if (optionIndex < (optionCount - 1)) {
        optionIndex++;
      } else {
        optionIndex = 0;
      }
      btn = options.children[optionIndex];
    }
    //set the theme & cookie
    let theme = btn.getAttribute('data-bs-theme');
    document.cookie = 'sunshineTheme=' + theme + ';';

    //remove all active classes from all buttons
    options.querySelectorAll('span').forEach((element) => {
      element.classList.remove('active');
    });


    //If it's the auto theme, determine what the computer prefers
    if (theme == "auto") {
      if (window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches) {
        theme = "dark";
      } else {
        theme = "light";
      }
    }

    //apply data attribute to html
    document.querySelector('html').setAttribute('data-bs-theme', theme);
    let icon = document.querySelector("#toggleTheme .active i");
    //icon.className = btn.query.className;
    btn.classList.add("active");
  }
}

sunshineUI.init();

function getCookie(name) {
  let value = `; ${document.cookie}`;
  let parts = value.split(`; ${name}=`);
  if (parts.length === 2) return parts.pop().split(';').shift();
}

function getChildNodes(elm) {
  var obj = {
    children: elm.parentNode.children,
    count: elm.parentNode.children.length
  }
  var c = obj.count;
  while (c--) {
    if (obj.children[c] == elm) {
      obj.index = (c);
    }
  }
  return obj;
}
