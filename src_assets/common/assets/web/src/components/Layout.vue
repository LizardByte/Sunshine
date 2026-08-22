<template>
    <Navbar></Navbar>
    <Notification></Notification>
    <div id="content" class="container">
        <router-view />
    </div>
</template>

<script>
import Navbar from './Navbar.vue'
import Notification from './Notification.vue'

export default {
    components: {
        Navbar,
        Notification
    }
}
</script>
