<script setup>
import Navbar from "./components/Navbar.vue";
</script>
<template>
  <div>
    <Navbar v-if="!$route.meta.hideNavbar"></Navbar>
    <RouterView :key="$route.fullPath" />
  </div>
</template>
