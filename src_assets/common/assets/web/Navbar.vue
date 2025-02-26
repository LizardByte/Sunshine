<template>
  <nav class="navbar navbar-light navbar-expand-lg navbar-background header">
    <div class="container-fluid">
      <a class="navbar-brand" href="/" title="Sunshine">
        <img src="/images/logo-sunshine-45.png" height="45" alt="Sunshine" />
      </a>
      <button
        class="navbar-toggler"
        type="button"
        data-bs-toggle="collapse"
        data-bs-target="#navbarSupportedContent"
        aria-controls="navbarSupportedContent"
        aria-expanded="false"
        aria-label="Toggle navigation"
      >
        <span class="navbar-toggler-icon"></span>
      </button>
      <div class="collapse navbar-collapse" id="navbarSupportedContent">
        <ul class="navbar-nav me-auto mb-2 mb-lg-0">
          <li class="nav-item">
            <a class="nav-link" href="/"><i class="fas fa-fw fa-home"></i> {{ $t('navbar.home') }}</a>
          </li>
          <li class="nav-item">
            <a class="nav-link" href="/pin"><i class="fas fa-fw fa-unlock"></i> {{ $t('navbar.pin') }}</a>
          </li>
          <li class="nav-item">
            <a class="nav-link" href="/apps"><i class="fas fa-fw fa-stream"></i> {{ $t('navbar.applications') }}</a>
          </li>
          <li class="nav-item">
            <a class="nav-link" href="/config"><i class="fas fa-fw fa-cog"></i> {{ $t('navbar.configuration') }}</a>
          </li>
          <li class="nav-item">
            <a class="nav-link" href="/password"
              ><i class="fas fa-fw fa-user-shield"></i> {{ $t('navbar.password') }}</a
            >
          </li>
          <li class="nav-item">
            <a class="nav-link" href="/troubleshooting"
              ><i class="fas fa-fw fa-info"></i> {{ $t('navbar.troubleshoot') }}</a
            >
          </li>
          <li class="nav-item">
            <ThemeToggle />
          </li>
        </ul>
      </div>
    </div>
  </nav>
</template>

<script setup>
import { onMounted, onUnmounted } from 'vue'
import ThemeToggle from './ThemeToggle.vue'

// 背景处理逻辑
const loadBackground = () => {
  const savedBg =
    localStorage.getItem('customBackground') ??
    'https://raw.gitmirror.com/qiin2333/qiin.github.io/assets/img/sunshine-bg0.webp'
  if (savedBg) {
    document.body.style.background = `url(${savedBg}) center/cover fixed no-repeat`
  }
}

// 拖拽事件处理
const handleDragOver = (e) => {
  e.preventDefault()
  document.body.classList.add('dragover')
}

const handleDragLeave = () => {
  document.body.classList.remove('dragover')
}

const handleDrop = (e) => {
  e.preventDefault()
  document.body.classList.remove('dragover')

  const file = e.dataTransfer.files[0]
  if (file?.type.startsWith('image/')) {
    const reader = new FileReader()
    reader.onload = (event) => {
      const bg = `url(${event.target.result})`
      document.body.style.background = `${bg} center/cover fixed no-repeat`
      localStorage.setItem('customBackground', bg)
    }
    reader.readAsDataURL(file)
  }
}

// 生命周期
onMounted(() => {
  loadBackground()

  // 当前路由高亮
  const el = document.querySelector(`a[href="${location.pathname}"]`)
  if (el) el.classList.add('active')

  // 添加事件监听
  document.addEventListener('dragover', handleDragOver)
  document.addEventListener('dragleave', handleDragLeave)
  document.addEventListener('drop', handleDrop)
})

onUnmounted(() => {
  // 清理事件监听
  document.removeEventListener('dragover', handleDragOver)
  document.removeEventListener('dragleave', handleDragLeave)
  document.removeEventListener('drop', handleDrop)
})
</script>

<style>
.navbar-background {
  background-color: #ffc400;
}

.header .nav-link {
  color: rgba(0, 0, 0, 0.65) !important;
}

.header .nav-link.active {
  color: rgb(0, 0, 0) !important;
  font-weight: 500;
}

.header .nav-link:hover {
  color: rgb(0, 0, 0) !important;
  font-weight: 500;
}

.header .navbar-toggler {
  color: rgba(var(--bs-dark-rgb), 0.65) !important;
  border: var(--bs-border-width) solid rgba(var(--bs-dark-rgb), 0.15) !important;
}

.header .navbar-toggler-icon {
  --bs-navbar-toggler-icon-bg: url("data:image/svg+xml,%3csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 30 30'%3e%3cpath stroke='rgba%2833, 37, 41, 0.75%29' stroke-linecap='round' stroke-miterlimit='10' stroke-width='2' d='M4 7h22M4 15h22M4 23h22'/%3e%3c/svg%3e") !important;
}

.form-control::placeholder {
  opacity: 0.5;
}

body {
  background-position: center;
  background-repeat: no-repeat;
  background-color: #5496dd;
  background-size: cover;
  background-attachment: fixed;
  transition: background 0.3s ease;
}

[data-bs-theme='light'] {
  --bs-body-bg: rgba(255, 255, 255, 0.3);
}
[data-bs-theme='dark'] {
  --bs-body-bg: rgba(0, 0, 0, 0.65);
}
.dragover {
  outline: 4px dashed #ffc400;
  outline-offset: -20px;
}
</style>
