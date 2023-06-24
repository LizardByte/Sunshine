<template>
	<div>
		<h1>Nuxt Routing set up successfully!</h1>
		<p>Current route: {{ route.path }}</p>
		<a href="https://nuxt.com/docs/getting-started/routing" target="_blank">Learn more about Nuxt Routing</a>
	</div>
</template>

<script setup lang="ts">
const route = useRoute();

onMounted(async () => {
	const res = await fetch("/api/config");
	const data = await res.json();
});
</script>
