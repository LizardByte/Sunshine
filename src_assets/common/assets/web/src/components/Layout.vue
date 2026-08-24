<template>
    <Navbar></Navbar>
    <Notification></Notification>
    <div id="content" class="container">
        <router-view />
    </div>
</template>

<script>
import Navbar from '@/components/Navbar.vue'
import Notification from '@/components/Notification.vue'

export default {
    components: {
        Navbar,
        Notification
    }
}
</script>
