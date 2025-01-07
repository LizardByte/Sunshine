document.addEventListener('DOMContentLoaded', function() {
  document.getElementById('new_comment').addEventListener('submit', function(event) {
    event.preventDefault();
    const form = this;

    form.classList.add('disabled');

    const endpoint = '';
    const repository = '';
    const branch = '';
    const url = endpoint + repository + '/' + branch + '/comments';
    const data = new URLSearchParams(new FormData(form)).toString();

    const xhr = new XMLHttpRequest();
    xhr.open("POST", url);
    xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
    xhr.setRequestHeader('X-Requested-With', 'XMLHttpRequest');
    xhr.onreadystatechange = function () {
      if(xhr.readyState === XMLHttpRequest.DONE) {
        const status = xhr.status;
        if (status >= 200 && status < 400) {
          formSubmitted();
        } else {
          formError();
        }
      }
    };

    function formSubmitted() {
      document.getElementById('comment-form-submit').classList.add('d-none');
      document.getElementById('comment-form-submitted').classList.remove('d-none');
      const notice = document.querySelector('.page__comments-form .js-notice');
      notice.classList.remove('alert-danger');
      notice.classList.add('alert-success');
      showAlert('success');
    }

    function formError() {
      document.getElementById('comment-form-submitted').classList.add('d-none');
      document.getElementById('comment-form-submit').classList.remove('d-none');
      const notice = document.querySelector('.page__comments-form .js-notice');
      notice.classList.remove('alert-success');
      notice.classList.add('alert-danger');
      showAlert('failure');
      form.classList.remove('disabled');
    }

    xhr.send(data);
  });

  function showAlert(message) {
    const notice = document.querySelector('.page__comments-form .js-notice');
    notice.classList.remove('d-none');
    if (message === 'success') {
      document.querySelector('.page__comments-form .js-notice-text-success').classList.remove('d-none');
      document.querySelector('.page__comments-form .js-notice-text-failure').classList.add('d-none');
    } else {
      document.querySelector('.page__comments-form .js-notice-text-success').classList.add('d-none');
      document.querySelector('.page__comments-form .js-notice-text-failure').classList.remove('d-none');
    }
  }
});
