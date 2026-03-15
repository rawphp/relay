import { defineConfig } from 'vitepress'

export default defineConfig({
  title: 'Relay',
  description: 'A persistent AI familiar daemon — bridges Telegram and Claude Code',

  themeConfig: {
    nav: [
      { text: 'User Guide', link: '/user/install' },
      { text: 'Developer Guide', link: '/developer/architecture' },
    ],

    sidebar: {
      '/user/': [
        {
          text: 'User Guide',
          items: [
            { text: 'Installation & Configuration', link: '/user/install' },
            { text: 'Operations', link: '/user/operations' },
            { text: 'Identity Files', link: '/user/identity' },
          ],
        },
      ],
      '/developer/': [
        {
          text: 'Developer Guide',
          items: [
            { text: 'Architecture', link: '/developer/architecture' },
            { text: 'Development', link: '/developer/development' },
          ],
        },
      ],
    },

    socialLinks: [],
  },
})
