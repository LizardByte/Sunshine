var sunshineUI = {
  init: function(){
    sunshineUI.toggleTheme();
    sunshineUI.getCurrentPage();

  },
  getCurrentPage: function(){
      let el = document.querySelector("a[href='" + document.location.pathname + "']");
      if (el) el.classList.add("active")
  },
	toggleTheme: function (btn) {
    let themeCookie = getCookie("sunshineTheme");
        
    if(!btn){
      //init page load
      let options = document.querySelector('#toggleTheme .dropdown-menu');
      if(themeCookie == null){
        //no cookie, get the active button
        btn = options.querySelector('.dropdown-item.active');
      }else{
        //cookie is set, make that button active
        btn = options.querySelectorAll('[data-bs-theme="'+themeCookie+'"]')[0];
      }
    }
    //set the theme & cookie
    let theme = btn.getAttribute('data-bs-theme');
    document.cookie = 'sunshineTheme='+theme+';';
    
    //remove all active classes from all buttons
    btn.closest('.dropdown-menu').querySelectorAll('button').forEach((element) => {
      element.classList.remove('active');
    });
    
    //If it's the auto theme, determine what the computer prefers
    if(theme == "auto"){
      if (window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches) {
        theme = "dark";
      }else{
        theme = "light";
      }
    }
    
    //apply data attribute to html
    document.querySelector('html').setAttribute('data-bs-theme', theme);
    let icon = document.querySelector("#toggleThemeMenu i");
    icon.className = btn.querySelector('i').className;
    btn.classList.add("active");
	}
}

//document click events
document.addEventListener('click', function (event) {
  //Toggle: Theme picker
  if (event.target.matches('#toggleTheme .dropdown-menu .dropdown-item')){
    sunshineUI.toggleTheme(event.target);
  };

}, false);

sunshineUI.init();

function getCookie (name) {
	let value = `; ${document.cookie}`;
	let parts = value.split(`; ${name}=`);
	if (parts.length === 2) return parts.pop().split(';').shift();
}