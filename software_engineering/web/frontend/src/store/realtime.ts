import { defineStore } from 'pinia'
import { useAuthStore } from './auth'

export type RtMsg = {
  kind: string
  [k: string]: any
}

export const useRealtimeStore = defineStore('realtime', {
  state: () => ({
    ws: null as WebSocket | null,
    connected: false,
    handlers: [] as ((m: RtMsg) => void)[],
  }),
  actions: {
    connect() {
      const auth = useAuthStore()
      if (!auth.token) return
      // 开发环境走 vite proxy /ws，生产走同源
      const proto = location.protocol === 'https:' ? 'wss' : 'ws'
      const url = `${proto}://${location.host}/ws/push?token=${encodeURIComponent(auth.token)}`
      const ws = new WebSocket(url)
      this.ws = ws
      ws.onopen = () => { this.connected = true }
      ws.onclose = () => {
        this.connected = false
        // 断线 3s 重连
        setTimeout(() => this.connect(), 3000)
      }
      ws.onmessage = (ev) => {
        try {
          const msg = JSON.parse(ev.data) as RtMsg
          for (const h of this.handlers) h(msg)
        } catch (e) {
          /* ignore */
        }
      }
    },
    on(cb: (m: RtMsg) => void) {
      this.handlers.push(cb)
    },
    off(cb: (m: RtMsg) => void) {
      this.handlers = this.handlers.filter((h) => h !== cb)
    },
  },
})
