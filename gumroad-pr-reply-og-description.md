Agreed that it *should* be in `<head>` and come from the description — that's the intent. The problem is custom-HTML pages never reach the code that does it.

I checked: setting the description doesn't help. This product has a 1,692-char description set, and `/l/joucal` still emits no `og:description`. Same account, both have descriptions — the only difference is custom_html:

```
curl -s https://lackluster.gumroad.com/l/paketti | grep -c 'property="og:description"'   # 1  (standard page)
curl -s https://lackluster.gumroad.com/l/joucal  | grep -c 'property="og:description"'   # 0  (custom_html page)
```

`og:description` is set in `PageMeta::Product#set_open_graph_meta` (from `plaintext_description`), but a custom-HTML product short-circuits before that runs. In `LinksController`:

```ruby
before_action :render_custom_html_if_present, only: [:show]
```

`render_custom_html_if_present` renders `custom_html_wrapper_document(...)` and returns, so the normal `show` path (which calls `set_product_page_meta`) never executes. And `custom_html_wrapper_document` hand-builds its own `<head>` — only `og:title` / `og:type` / `og:url` / `og:image`:

```ruby
<title>#{title}</title>
<link rel="canonical" href="#{canonical}">
<meta property="og:title" content="#{title}">
<meta property="og:type" content="product">
<meta property="og:url" content="#{canonical}">
#{og_image_tag}
```

So custom HTML *is* affecting it — `og:description`, `fb:app_id`, and all `twitter:*` are silently dropped for any product with a custom landing. This PR just emits those same description-derived tags in the wrapper `<head>`, mirroring `PageMeta::Product`, so the two paths reach parity.

Happy to add a request-level spec (`GET /l/:id` for a custom_html product asserts `og:description` is in the body) on top of the controller spec if that's clearer than the wrapper-method test.
