(() => {
  'use strict';

  document.querySelectorAll('.store-gallery-modal').forEach((modalElement) => {
    const carouselElement = modalElement.querySelector('.store-gallery-carousel');
    if (!carouselElement) {
      return;
    }

    modalElement.addEventListener('show.bs.modal', (event) => {
      const requestedSlide = Number.parseInt(event.relatedTarget?.dataset.storeSlide ?? '0', 10);
      const carousel = bootstrap.Carousel.getOrCreateInstance(carouselElement, {
        interval: false,
        keyboard: true,
        touch: true,
        wrap: true,
      });

      carousel.to(Number.isNaN(requestedSlide) ? 0 : requestedSlide);
    });

    modalElement.addEventListener('shown.bs.modal', () => {
      carouselElement.focus();
    });
  });
})();
