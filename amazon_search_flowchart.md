# Browser Subagent Workflow

Here is exactly how I processed your request to search for the iPhone 17 on Amazon. My subagent operates by taking in your instructions, reading the webpage like a human would, and simulating mouse clicks and keyboard presses!

### The Workflow Flowchart

```mermaid
flowchart TD
    A[Start Browser Subagent] --> B[Open Hidden Browser Window]
    B --> C[Navigate to https://www.amazon.in/]
    C --> D[Analyze Page HTML / DOM Structure]
    D --> E[Locate Search Bar User Interface Element]
    E --> F[Move Mouse & Click Search Bar X:633, Y:38]
    F --> G[Type "iPhone 17" & Click Search X:800, Y:39]
    G --> H[Wait for Page to Load]
    H --> I[Analyze Search Results HTML]
    I --> J[Locate 'iPhone 17 Pro' Listing & Click it X:740, Y:686]
    J --> K[Wait for Product Page to Load]
    K --> L[Extract Specs: ₹1,54,900, 512GB, Cosmic Orange]
    L --> M[Compile Report & End Session]
```

### Video Recording of the Task
Here is the actual video of the hidden browser performing your request!

![Amazon iPhone 17 Search Recording](C:/Users/daksh/.gemini/antigravity/brain/c9449059-63dd-48f9-b78c-6bb7e1c852a4/amazon_iphone_17_1774942100635.webp)
