<template>
  <el-container class="layout">
    <!-- PC 侧边栏（≥768px 常驻） -->
    <el-aside v-if="!isMobile" width="200px" class="aside">
      <div class="logo">人脸门禁</div>
      <el-menu :default-active="route.path" router>
        <el-menu-item index="/dashboard"><el-icon><Monitor /></el-icon><span>仪表盘</span></el-menu-item>
        <el-menu-item index="/door"><el-icon><Unlock /></el-icon><span>远程开门</span></el-menu-item>
        <el-menu-item index="/users"><el-icon><User /></el-icon><span>人员管理</span></el-menu-item>
        <el-menu-item index="/records"><el-icon><List /></el-icon><span>通行记录</span></el-menu-item>
        <el-menu-item index="/logs"><el-icon><Document /></el-icon><span>远程日志</span></el-menu-item>
        <el-menu-item index="/ota"><el-icon><Upload /></el-icon><span>OTA 升级</span></el-menu-item>
      </el-menu>
    </el-aside>

    <!-- 手机抽屉 -->
    <el-drawer v-model="drawer" direction="ltr" size="200px" :with-header="false" v-if="isMobile">
      <el-menu :default-active="route.path" router @select="drawer = false">
        <el-menu-item index="/dashboard"><el-icon><Monitor /></el-icon><span>仪表盘</span></el-menu-item>
        <el-menu-item index="/door"><el-icon><Unlock /></el-icon><span>远程开门</span></el-menu-item>
        <el-menu-item index="/users"><el-icon><User /></el-icon><span>人员管理</span></el-menu-item>
        <el-menu-item index="/records"><el-icon><List /></el-icon><span>通行记录</span></el-menu-item>
        <el-menu-item index="/logs"><el-icon><Document /></el-icon><span>远程日志</span></el-menu-item>
        <el-menu-item index="/ota"><el-icon><Upload /></el-icon><span>OTA 升级</span></el-menu-item>
      </el-menu>
    </el-drawer>

    <el-container>
      <el-header class="header">
        <el-icon v-if="isMobile" class="burger" @click="drawer = true"><Menu /></el-icon>
        <span class="title">人脸门禁管理后台</span>
        <el-tag v-if="rt.connected" type="success" size="small" effect="plain">实时在线</el-tag>
        <el-tag v-else type="info" size="small" effect="plain">离线</el-tag>
        <div class="spacer" />
        <span class="user">{{ auth.username }}</span>
        <el-button text @click="logout">退出</el-button>
      </el-header>
      <el-main class="main">
        <router-view />
      </el-main>
    </el-container>
  </el-container>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useAuthStore } from '../store/auth'
import { useRealtimeStore } from '../store/realtime'

const route = useRoute()
const router = useRouter()
const auth = useAuthStore()
const rt = useRealtimeStore()

const isMobile = ref(window.innerWidth < 768)
const drawer = ref(false)

function onResize() { isMobile.value = window.innerWidth < 768 }
onMounted(() => {
  window.addEventListener('resize', onResize)
  if (!rt.connected) rt.connect()
})
onUnmounted(() => window.removeEventListener('resize', onResize))

function logout() {
  auth.clear()
  router.push({ name: 'login' })
}
</script>

<style scoped>
.layout { height: 100vh; }
.aside { background: #fff; border-right: 1px solid #e6e8eb; }
.logo { height: 56px; line-height: 56px; text-align: center; font-weight: 600; color: #409eff; }
.header { display: flex; align-items: center; gap: 8px; background: #fff; border-bottom: 1px solid #e6e8eb; }
.burger { font-size: 22px; cursor: pointer; }
.title { font-weight: 600; }
.spacer { flex: 1; }
.user { color: #606266; font-size: 14px; }
.main { padding: 12px; }
</style>
