/**
 * @file api-tokens.js
 * @brief Entry point for the API Token Management page
 */
import { createApp } from 'vue';
import { initApp } from './init';
import Navbar from './Navbar.vue';
import ApiTokenManager from './ApiTokenManager.vue';

const app = createApp({
  components: { 
    Navbar,
    ApiTokenManager
  }
});

initApp(app);
